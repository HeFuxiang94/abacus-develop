#include "esolver_ks_pw.h"

#include <iostream>

#include "module_io/nscf_band.h"
#include "module_io/write_dos_pw.h"
#include "module_io/write_istate_info.h"
#include "module_io/write_wfc_pw.h"

//--------------temporary----------------------------
#include "module_elecstate/module_charge/symmetry_rho.h"
#include "module_elecstate/occupy.h"
#include "module_hamilt_general/module_ewald/H_Ewald_pw.h"
#include "module_hamilt_pw/hamilt_pwdft/global.h"
#include "module_io/print_info.h"
//-----force-------------------
#include "module_hamilt_pw/hamilt_pwdft/forces.h"
//-----stress------------------
#include "module_hamilt_pw/hamilt_pwdft/stress_pw.h"
//---------------------------------------------------
#include "module_base/memory.h"
#include "module_elecstate/elecstate_pw.h"
#include "module_hamilt_general/module_vdw/vdw.h"
#include "module_hamilt_pw/hamilt_pwdft/hamilt_pw.h"
#include "module_hsolver/diago_iter_assist.h"
#include "module_hsolver/hsolver_pw.h"
#include "module_hsolver/kernels/dngvd_op.h"
#include "module_hsolver/kernels/math_kernel_op.h"
#include "module_io/berryphase.h"
#include "module_io/numerical_basis.h"
#include "module_io/numerical_descriptor.h"
#include "module_io/rho_io.h"
#include "module_io/to_wannier90.h"
#include "module_io/winput.h"
#include "module_io/write_wfc_r.h"
#include "module_psi/kernels/device.h"

namespace ModuleESolver
{

template <typename FPTYPE, typename Device>
ESolver_KS_PW<FPTYPE, Device>::ESolver_KS_PW()
{
    this->classname = "ESolver_KS_PW";
    this->basisname = "PW";
    this->device = psi::device::get_device_type<Device>(this->ctx);
#if ((defined __CUDA) || (defined __ROCM))
    if (this->device == psi::GpuDevice)
    {
        hsolver::createBLAShandle();
        hsolver::createCUSOLVERhandle();
    }
#endif
}

template <typename FPTYPE, typename Device>
ESolver_KS_PW<FPTYPE, Device>::~ESolver_KS_PW()
{
    // delete HSolver and ElecState
    if (this->phsol != nullptr)
    {
        delete reinterpret_cast<hsolver::HSolverPW<FPTYPE, Device>*>(this->phsol);
        this->phsol = nullptr;
    }
    if (this->pelec != nullptr)
    {
        delete reinterpret_cast<elecstate::ElecStatePW<FPTYPE, Device>*>(this->pelec);
        this->pelec = nullptr;
    }
    // delete Hamilt
    if (this->p_hamilt != nullptr)
    {
        delete reinterpret_cast<hamilt::HamiltPW<FPTYPE, Device>*>(this->p_hamilt);
        this->p_hamilt = nullptr;
    }
    if (this->device == psi::GpuDevice)
    {
#if defined(__CUDA) || defined(__ROCM)
        hsolver::destoryBLAShandle();
        hsolver::destoryCUSOLVERhandle();
#endif
        delete reinterpret_cast<psi::Psi<std::complex<FPTYPE>, Device>*>(this->kspw_psi);
    }
    if (GlobalV::precision_flag == "single")
    {
        delete reinterpret_cast<psi::Psi<std::complex<double>, Device>*>(this->__kspw_psi);
    }
}

template <typename FPTYPE, typename Device>
void ESolver_KS_PW<FPTYPE, Device>::Init_GlobalC(Input& inp, UnitCell& cell)
{
    if (this->psi != nullptr)
        delete this->psi;
    this->psi = this->wf.allocate(this->kv.nks, this->kv.ngk.data(), this->pw_wfc->npwk_max);

    // cout<<this->pw_rho->nrxx<<endl;
    // cout<<"before ufft allocate"<<endl;

    // cout<<"after ufft allocate"<<endl;

    //=======================
    // init pseudopotential
    //=======================
    GlobalC::ppcell.init(GlobalC::ucell.ntype, &this->sf, this->pw_wfc);

    //=====================
    // init hamiltonian
    // only allocate in the beginning of ELEC LOOP!
    //=====================
    // not used anymore
    // GlobalC::hm.hpw.allocate(this->wf.npwx, GlobalV::NPOL, GlobalC::ppcell.nkb, this->pw_rho->nrxx);

    //=================================
    // initalize local pseudopotential
    //=================================
    GlobalC::ppcell.init_vloc(GlobalC::ppcell.vloc, this->pw_rho);
    ModuleBase::GlobalFunc::DONE(GlobalV::ofs_running, "LOCAL POTENTIAL");

    //======================================
    // Initalize non local pseudopotential
    //======================================
    GlobalC::ppcell.init_vnl(GlobalC::ucell);
    ModuleBase::GlobalFunc::DONE(GlobalV::ofs_running, "NON-LOCAL POTENTIAL");

    GlobalC::ppcell.cal_effective_D();

    //==================================================
    // create GlobalC::ppcell.tab_at , for trial wave functions.
    //==================================================
    this->wf.init_at_1(&this->sf);

    //================================
    // Initial start wave functions
    //================================
    this->wf.wfcinit(this->psi, this->pw_wfc);

    // denghui added 20221116
    this->kspw_psi = GlobalV::device_flag == "gpu" || GlobalV::precision_flag == "single"
                         ? new psi::Psi<std::complex<FPTYPE>, Device>(this->psi[0])
                         : reinterpret_cast<psi::Psi<std::complex<FPTYPE>, Device>*>(this->psi);
    if (GlobalV::precision_flag == "single")
    {
        ModuleBase::Memory::record("Psi_single", sizeof(std::complex<FPTYPE>) * this->psi[0].size());
    }

    ModuleBase::GlobalFunc::DONE(GlobalV::ofs_running, "INIT BASIS");
}

template <typename FPTYPE, typename Device>
void ESolver_KS_PW<FPTYPE, Device>::Init(Input& inp, UnitCell& ucell)
{
    ESolver_KS<FPTYPE, Device>::Init(inp, ucell);

    // init HSolver
    if (this->phsol == nullptr)
    {
        this->phsol = new hsolver::HSolverPW<FPTYPE, Device>(this->pw_wfc, &this->wf);
    }

    // init ElecState,
    if (this->pelec == nullptr)
    {
        this->pelec = new elecstate::ElecStatePW<FPTYPE, Device>(this->pw_wfc,
                                                                 &(this->chr),
                                                                 &(this->kv),
                                                                 this->pw_rho,
                                                                 this->pw_big);
    }

    // Inititlize the charge density.
    this->pelec->charge->allocate(GlobalV::NSPIN);
    this->pelec->omega = GlobalC::ucell.omega;

    // Initialize the potential.
    if (this->pelec->pot == nullptr)
    {
        this->pelec->pot = new elecstate::Potential(this->pw_rho,
                                                    &GlobalC::ucell,
                                                    &(GlobalC::ppcell.vloc),
                                                    &(this->sf),
                                                    &(this->pelec->f_en.etxc),
                                                    &(this->pelec->f_en.vtxc));
    }

    // temporary
    this->Init_GlobalC(inp, ucell);

    // Fix pelec->wg by ocp_kb
    if (GlobalV::ocp)
    {
        this->pelec->fixed_weights(GlobalV::ocp_kb);
    }
}

template <typename FPTYPE, typename Device>
void ESolver_KS_PW<FPTYPE, Device>::init_after_vc(Input& inp, UnitCell& ucell)
{
    ModuleBase::TITLE("ESolver_KS_PW", "init_after_vc");
    ModuleBase::timer::tick("ESolver_KS_PW", "init_after_vc");

    ESolver_KS<FPTYPE, Device>::init_after_vc(inp, ucell);

    if (GlobalV::md_prec_level == 2)
    {
        this->pw_wfc->initgrids(ucell.lat0, ucell.latvec, this->pw_rho->nx, this->pw_rho->ny, this->pw_rho->nz);
        this->pw_wfc->initparameters(false, inp.ecutwfc, this->kv.nks, this->kv.kvec_d.data());
#ifdef __MPI
        if (INPUT.pw_seed > 0)
            MPI_Allreduce(MPI_IN_PLACE, &this->pw_wfc->ggecut, 1, MPI_DOUBLE, MPI_MAX, MPI_COMM_WORLD);
            // qianrui add 2021-8-13 to make different kpar parameters can get the same results
#endif
        this->pw_wfc->setuptransform();
        for (int ik = 0; ik < this->kv.nks; ++ik)
            this->kv.ngk[ik] = this->pw_wfc->npwk[ik];
        this->pw_wfc->collect_local_pw();

        delete this->phsol;
        this->phsol = new hsolver::HSolverPW<FPTYPE, Device>(this->pw_wfc, &this->wf);

        delete this->pelec;
        this->pelec = new elecstate::ElecStatePW<FPTYPE, Device>(this->pw_wfc,
                                                                 &(this->chr),
                                                                 (K_Vectors*)(&(this->kv)),
                                                                 this->pw_rho,
                                                                 this->pw_big);

        this->pelec->charge->allocate(GlobalV::NSPIN);
        this->pelec->omega = GlobalC::ucell.omega;

        delete this->pelec->pot;
        this->pelec->pot = new elecstate::Potential(this->pw_rho,
                                                    &GlobalC::ucell,
                                                    &(GlobalC::ppcell.vloc),
                                                    &(this->sf),
                                                    &(this->pelec->f_en.etxc),
                                                    &(this->pelec->f_en.vtxc));

        // temporary
        this->Init_GlobalC(inp, ucell);
    }
    else if (GlobalV::md_prec_level == 1)
    {
        GlobalC::ppcell.init_vnl(GlobalC::ucell);
        ModuleBase::GlobalFunc::DONE(GlobalV::ofs_running, "NON-LOCAL POTENTIAL");

        this->wf.init_after_vc(this->kv.nks);
        this->wf.init_at_1(&this->sf);
    }
    else if (GlobalV::md_prec_level == 0)
    {
        GlobalC::ppcell.init_vnl(GlobalC::ucell);
        ModuleBase::GlobalFunc::DONE(GlobalV::ofs_running, "NON-LOCAL POTENTIAL");

        this->pw_wfc->initgrids(GlobalC::ucell.lat0,
                                GlobalC::ucell.latvec,
                                this->pw_wfc->nx,
                                this->pw_wfc->ny,
                                this->pw_wfc->nz);
        this->pw_wfc->initparameters(false, INPUT.ecutwfc, this->kv.nks, this->kv.kvec_d.data());
        this->pw_wfc->collect_local_pw();
        this->wf.init_after_vc(this->kv.nks);
        this->wf.init_at_1(&this->sf);
    }
    ModuleBase::timer::tick("ESolver_KS_PW", "init_after_vc");
}

template <typename FPTYPE, typename Device>
void ESolver_KS_PW<FPTYPE, Device>::beforescf(int istep)
{
    ModuleBase::TITLE("ESolver_KS_PW", "beforescf");

    if (GlobalC::ucell.cell_parameter_updated)
    {
        this->init_after_vc(INPUT, GlobalC::ucell);
    }
    if (GlobalC::ucell.ionic_position_updated && GlobalV::md_prec_level != 2)
    {
        this->CE.update_all_dis(GlobalC::ucell);
        this->CE.extrapolate_charge(this->pelec->charge, &this->sf);
    }

    // init Hamilt, this should be allocated before each scf loop
    // Operators in HamiltPW should be reallocated once cell changed
    // delete Hamilt if not first scf
    if (this->p_hamilt != nullptr)
    {
        delete reinterpret_cast<hamilt::HamiltPW<FPTYPE, Device>*>(this->p_hamilt);
        this->p_hamilt = nullptr;
    }
    // allocate HamiltPW
    if (this->p_hamilt == nullptr)
    {
        this->p_hamilt = new hamilt::HamiltPW<FPTYPE, Device>(this->pelec->pot, this->pw_wfc, &this->kv);
    }

    //----------------------------------------------------------
    // about vdw, jiyy add vdwd3 and linpz add vdwd2
    //----------------------------------------------------------
    auto vdw_solver = vdw::make_vdw(GlobalC::ucell, INPUT);
    if (vdw_solver != nullptr)
    {
        this->pelec->f_en.evdw = vdw_solver->get_energy();
    }

    // calculate ewald energy
    if (!GlobalV::test_skip_ewald)
    {
        this->pelec->f_en.ewald_energy = H_Ewald_pw::compute_ewald(GlobalC::ucell, this->pw_rho, this->sf.strucFac);
    }

    //=========================================================
    // calculate the total local pseudopotential in real space
    //=========================================================
    this->pelec->init_scf(istep, this->sf.strucFac);
    // Symmetry_rho should behind init_scf, because charge should be initialized first.
    Symmetry_rho srho;
    for (int is = 0; is < GlobalV::NSPIN; is++)
    {
        srho.begin(is, *(this->pelec->charge), this->pw_rho, GlobalC::Pgrid, this->symm);
    }
}

template <typename FPTYPE, typename Device>
void ESolver_KS_PW<FPTYPE, Device>::othercalculation(const int istep)
{
    ModuleBase::TITLE("ESolver_KS_PW", "othercalculation");
    ModuleBase::timer::tick("ESolver_KS_PW", "othercalculation");
    if (GlobalV::CALCULATION == "test_memory")
    {
        Cal_Test::test_memory(this->pw_rho,
                              this->pw_wfc,
                              this->p_chgmix->get_mixing_mode(),
                              this->p_chgmix->get_mixing_ndim());
        return;
    }

    if (GlobalV::CALCULATION == "gen_bessel")
    {
        // caoyu add 2020-11-24, mohan updat 2021-01-03
        Numerical_Descriptor nc;
        nc.output_descriptor(this->psi[0],
                             INPUT.bessel_descriptor_lmax,
                             INPUT.bessel_descriptor_rcut,
                             INPUT.bessel_descriptor_tolerence,
                             this->kv.nks);
        ModuleBase::GlobalFunc::DONE(GlobalV::ofs_running, "GENERATE DESCRIPTOR FOR DEEPKS");
        return;
    }

    // self consistent calculations for electronic ground state
    if (GlobalV::CALCULATION == "nscf")
    {
        this->nscf();
    }
    else
    {
        ModuleBase::WARNING_QUIT("ESolver_KS_LCAO::othercalculation", "CALCULATION type not supported");
    }

    ModuleBase::timer::tick("ESolver_KS_PW", "othercalculation");
    return;
}

template <typename FPTYPE, typename Device>
void ESolver_KS_PW<FPTYPE, Device>::eachiterinit(const int istep, const int iter)
{
    // mohan add 2010-07-16
    if (iter == 1)
        this->p_chgmix->reset();

    // mohan move harris functional to here, 2012-06-05
    // use 'rho(in)' and 'v_h and v_xc'(in)
    this->pelec->f_en.deband_harris = this->pelec->cal_delta_eband();

    //(2) save change density as previous charge,
    // prepared fox mixing.
    if (GlobalV::MY_STOGROUP == 0)
    {
        this->pelec->charge->save_rho_before_sum_band();
    }
}

// Temporary, it should be replaced by hsolver later.
template <typename FPTYPE, typename Device>
void ESolver_KS_PW<FPTYPE, Device>::hamilt2density(const int istep, const int iter, const FPTYPE ethr)
{
    if (this->phsol != nullptr)
    {
        // reset energy
        this->pelec->f_en.eband = 0.0;
        this->pelec->f_en.demet = 0.0;
        // choose if psi should be diag in subspace
        // be careful that istep start from 0 and iter start from 1
        // if (iter == 1)
        if ((istep == 0 || istep == 1) && iter == 1)
        {
            hsolver::DiagoIterAssist<FPTYPE, Device>::need_subspace = false;
        }
        else
        {
            hsolver::DiagoIterAssist<FPTYPE, Device>::need_subspace = true;
        }

        hsolver::DiagoIterAssist<FPTYPE, Device>::PW_DIAG_THR = ethr;
        hsolver::DiagoIterAssist<FPTYPE, Device>::PW_DIAG_NMAX = GlobalV::PW_DIAG_NMAX;
        this->phsol->solve(this->p_hamilt, this->kspw_psi[0], this->pelec, GlobalV::KS_SOLVER);

        if (GlobalV::out_bandgap)
        {
            if (!GlobalV::TWO_EFERMI)
            {
                this->pelec->cal_bandgap();
            }
            else
            {
                this->pelec->cal_bandgap_updw();
            }
        }
    }
    else
    {
        ModuleBase::WARNING_QUIT("ESolver_KS_PW", "HSolver has not been initialed!");
    }

    // add exx
#ifdef __LCAO
#ifdef __EXX
    this->pelec->set_exx(); // Peize Lin add 2019-03-09
#endif
#endif
    // calculate the delta_harris energy
    // according to new charge density.
    // mohan add 2009-01-23
    this->pelec->cal_energies(1);
    Symmetry_rho srho;
    for (int is = 0; is < GlobalV::NSPIN; is++)
    {
        srho.begin(is, *(this->pelec->charge), this->pw_rho, GlobalC::Pgrid, this->symm);
    }

    // compute magnetization, only for LSDA(spin==2)
    GlobalC::ucell.magnet.compute_magnetization(this->pelec->charge->nrxx,
                                                this->pelec->charge->nxyz,
                                                this->pelec->charge->rho,
                                                this->pelec->nelec_spin.data());
    // deband is calculated from "output" charge density calculated
    // in sum_band
    // need 'rho(out)' and 'vr (v_h(in) and v_xc(in))'

    this->pelec->f_en.deband = this->pelec->cal_delta_eband();
    // if (LOCAL_BASIS) xiaohui modify 2013-09-02
}

// Temporary, it should be rewritten with Hamilt class.
template <typename FPTYPE, typename Device>
void ESolver_KS_PW<FPTYPE, Device>::updatepot(const int istep, const int iter)
{
    if (!this->conv_elec)
    {
        if (GlobalV::NSPIN == 4)
            GlobalC::ucell.cal_ux();
        this->pelec->pot->update_from_charge(this->pelec->charge, &GlobalC::ucell);
        this->pelec->f_en.descf = this->pelec->cal_delta_escf();
    }
    else
    {
        this->pelec->cal_converged();
    }
}

template <typename FPTYPE, typename Device>
void ESolver_KS_PW<FPTYPE, Device>::eachiterfinish(const int iter)
{
    // print_eigenvalue(GlobalV::ofs_running);
    this->pelec->cal_energies(2);
    // We output it for restarting the scf.
    bool print = false;
    if (this->out_freq_elec && iter % this->out_freq_elec == 0)
    {
        print = true;
    }

    if (print)
    {
        if (GlobalV::out_chg > 0)
        {
            for (int is = 0; is < GlobalV::NSPIN; is++)
            {
                std::stringstream ssc;
                ssc << GlobalV::global_out_dir << "tmp"
                    << "_SPIN" << is + 1 << "_CHG.cube";
                const double ef_tmp = this->pelec->eferm.get_efval(is);
                ModuleIO::write_rho(
#ifdef __MPI
                    this->pw_big->bz,
                    this->pw_big->nbz,
                    this->pw_rho->nplane,
                    this->pw_rho->startz_current,
#endif
                    this->pelec->charge->rho_save[is],
                    is,
                    GlobalV::NSPIN,
                    iter,
                    ssc.str(),
                    this->pw_rho->nx,
                    this->pw_rho->ny,
                    this->pw_rho->nz,
                    ef_tmp,
                    &(GlobalC::ucell),
                    3);
            }
            if (XC_Functional::get_func_type() == 3 || XC_Functional::get_func_type() == 5)
            {
                for (int is = 0; is < GlobalV::NSPIN; is++)
                {
                    std::stringstream ssc;
                    ssc << GlobalV::global_out_dir << "tmp"
                        << "_SPIN" << is + 1 << "_TAU.cube";
                    const double ef_tmp = this->pelec->eferm.get_efval(is);
                    ModuleIO::write_rho(
#ifdef __MPI
                        this->pw_big->bz,
                        this->pw_big->nbz,
                        this->pw_rho->nplane,
                        this->pw_rho->startz_current,
#endif
                        this->pelec->charge->kin_r_save[is],
                        is,
                        GlobalV::NSPIN,
                        iter,
                        ssc.str(),
                        this->pw_rho->nx,
                        this->pw_rho->ny,
                        this->pw_rho->nz,
                        ef_tmp,
                        &(GlobalC::ucell),
                        3);
                }
            }
        }
        // output wavefunctions
        if (this->wf.out_wfc_pw == 1 || this->wf.out_wfc_pw == 2)
        {
            std::stringstream ssw;
            ssw << GlobalV::global_out_dir << "WAVEFUNC";
            // mohan update 2011-02-21
            // qianrui update 2020-10-17
            ModuleIO::write_wfc_pw(ssw.str(), this->psi[0], this->kv, this->pw_wfc);
            // ModuleBase::GlobalFunc::DONE(GlobalV::ofs_running,"write wave functions into file WAVEFUNC.dat");
        }
    }
}

template <typename FPTYPE, typename Device>
void ESolver_KS_PW<FPTYPE, Device>::afterscf(const int istep)
{
    if (GlobalV::out_pot == 1) // output the effective potential, sunliang 2023-03-16
    {
        for (int is = 0; is < GlobalV::NSPIN; is++)
        {
            int precision = 3; // be consistent with esolver_ks_lcao.cpp
            std::stringstream ssp;
            ssp << GlobalV::global_out_dir << "SPIN" << is + 1 << "_POT.cube";
            this->pelec->pot->write_potential(
#ifdef __MPI
                this->pw_big->bz,
                this->pw_big->nbz,
                this->pw_rho->nplane,
                this->pw_rho->startz_current,
#endif
                is,
                0,
                ssp.str(),
                this->pw_rho->nx,
                this->pw_rho->ny,
                this->pw_rho->nz,
                this->pelec->pot->get_effective_v(),
                precision);
        }
    }

    if (GlobalV::out_chg)
    {
        for (int is = 0; is < GlobalV::NSPIN; is++)
        {
            std::stringstream ssc;
            ssc << GlobalV::global_out_dir << "SPIN" << is + 1 << "_CHG.cube";
            const double ef_tmp = this->pelec->eferm.get_efval(is);
            ModuleIO::write_rho(
#ifdef __MPI
                this->pw_big->bz,
                this->pw_big->nbz,
                this->pw_rho->nplane,
                this->pw_rho->startz_current,
#endif
                this->pelec->charge->rho_save[is],
                is,
                GlobalV::NSPIN,
                0,
                ssc.str(),
                this->pw_rho->nx,
                this->pw_rho->ny,
                this->pw_rho->nz,
                ef_tmp,
                &(GlobalC::ucell));
        }
        if (XC_Functional::get_func_type() == 3 || XC_Functional::get_func_type() == 5)
        {
            for (int is = 0; is < GlobalV::NSPIN; is++)
            {
                std::stringstream ssc;
                ssc << GlobalV::global_out_dir << "SPIN" << is + 1 << "_TAU.cube";
                const double ef_tmp = this->pelec->eferm.get_efval(is);
                ModuleIO::write_rho(
#ifdef __MPI
                    this->pw_big->bz,
                    this->pw_big->nbz,
                    this->pw_rho->nplane,
                    this->pw_rho->startz_current,
#endif
                    this->pelec->charge->kin_r_save[is],
                    is,
                    GlobalV::NSPIN,
                    0,
                    ssc.str(),
                    this->pw_rho->nx,
                    this->pw_rho->ny,
                    this->pw_rho->nz,
                    ef_tmp,
                    &(GlobalC::ucell));
            }
        }
    }

    if (this->wf.out_wfc_pw == 1 || this->wf.out_wfc_pw == 2)
    {
        std::stringstream ssw;
        ssw << GlobalV::global_out_dir << "WAVEFUNC";
        ModuleIO::write_wfc_pw(ssw.str(), this->psi[0], this->kv, this->pw_wfc);
    }
    if (this->conv_elec)
    {
        GlobalV::ofs_running << "\n charge density convergence is achieved" << std::endl;
        GlobalV::ofs_running << " final etot is " << this->pelec->f_en.etot * ModuleBase::Ry_to_eV << " eV"
                             << std::endl;
    }
    else
    {
        GlobalV::ofs_running << " convergence has NOT been achieved!" << std::endl;
    }

    if (GlobalV::out_pot == 2)
    {
        std::stringstream ssp;
        std::stringstream ssp_ave;
        ssp << GlobalV::global_out_dir << "ElecStaticPot.cube";
        // ssp_ave << GlobalV::global_out_dir << "ElecStaticPot_AVE";
        this->pelec->pot->write_elecstat_pot(
#ifdef __MPI
            this->pw_big->bz,
            this->pw_big->nbz,
#endif
            ssp.str(),
            this->pw_rho,
            this->pelec->charge); // output 'Hartree + local pseudopot'
    }

    if (GlobalV::OUT_LEVEL != "m")
    {
        this->pelec->print_eigenvalue(GlobalV::ofs_running);
    }
    if (this->device == psi::GpuDevice)
    {
        castmem_2d_d2h_op()(this->psi[0].get_device(),
                            this->kspw_psi[0].get_device(),
                            this->psi[0].get_pointer() - this->psi[0].get_psi_bias(),
                            this->kspw_psi[0].get_pointer() - this->kspw_psi[0].get_psi_bias(),
                            this->psi[0].size());
    }
}

template <typename FPTYPE, typename Device>
double ESolver_KS_PW<FPTYPE, Device>::cal_Energy()
{
    return this->pelec->f_en.etot;
}

template <typename FPTYPE, typename Device>
void ESolver_KS_PW<FPTYPE, Device>::cal_Force(ModuleBase::matrix& force)
{
    Forces<double, Device> ff(GlobalC::ucell.nat);
    if (this->__kspw_psi != nullptr)
        this->__kspw_psi = nullptr;
    if (this->__kspw_psi == nullptr)
    {
        this->__kspw_psi = GlobalV::precision_flag == "single"
                               ? new psi::Psi<std::complex<double>, Device>(this->kspw_psi[0])
                               : reinterpret_cast<psi::Psi<std::complex<double>, Device>*>(this->kspw_psi);
    }
    ff.cal_force(force, *this->pelec, this->pw_rho, &this->symm, &this->sf, &this->kv, this->pw_wfc, this->__kspw_psi);
}

template <typename FPTYPE, typename Device>
void ESolver_KS_PW<FPTYPE, Device>::cal_Stress(ModuleBase::matrix& stress)
{
    Stress_PW<double, Device> ss(this->pelec);
    if (this->__kspw_psi != nullptr)
        this->__kspw_psi = nullptr;
    if (this->__kspw_psi == nullptr)
    {
        this->__kspw_psi = GlobalV::precision_flag == "single"
                               ? new psi::Psi<std::complex<double>, Device>(this->kspw_psi[0])
                               : reinterpret_cast<psi::Psi<std::complex<double>, Device>*>(this->kspw_psi);
    }
    ss.cal_stress(stress,
                  GlobalC::ucell,
                  this->pw_rho,
                  &this->symm,
                  &this->sf,
                  &this->kv,
                  this->pw_wfc,
                  this->psi,
                  this->__kspw_psi);

    // external stress
    double unit_transform = 0.0;
    unit_transform = ModuleBase::RYDBERG_SI / pow(ModuleBase::BOHR_RADIUS_SI, 3) * 1.0e-8;
    double external_stress[3] = {GlobalV::PRESS1, GlobalV::PRESS2, GlobalV::PRESS3};
    for (int i = 0; i < 3; i++)
    {
        stress(i, i) -= external_stress[i] / unit_transform;
    }
    GlobalV::PRESSURE = (stress(0, 0) + stress(1, 1) + stress(2, 2)) / 3;
}

template <typename FPTYPE, typename Device>
void ESolver_KS_PW<FPTYPE, Device>::postprocess()
{

    GlobalV::ofs_running << "\n\n --------------------------------------------" << std::endl;
    GlobalV::ofs_running << std::setprecision(16);
    GlobalV::ofs_running << " !FINAL_ETOT_IS " << this->pelec->f_en.etot * ModuleBase::Ry_to_eV << " eV" << std::endl;
    GlobalV::ofs_running << " --------------------------------------------\n\n" << std::endl;

    if (INPUT.out_dos != 0 || INPUT.out_band != 0)
    {
        GlobalV::ofs_running << "\n\n\n\n";
        GlobalV::ofs_running << " >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>" << std::endl;
        GlobalV::ofs_running << " |                                                                    |" << std::endl;
        GlobalV::ofs_running << " | Post-processing of data:                                           |" << std::endl;
        GlobalV::ofs_running << " | DOS (density of states) and bands will be output here.             |" << std::endl;
        GlobalV::ofs_running << " | If atomic orbitals are used, Mulliken charge analysis can be done. |" << std::endl;
        GlobalV::ofs_running << " | Also the .bxsf file containing fermi surface information can be    |" << std::endl;
        GlobalV::ofs_running << " | done here.                                                         |" << std::endl;
        GlobalV::ofs_running << " |                                                                    |" << std::endl;
        GlobalV::ofs_running << " <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<" << std::endl;
        GlobalV::ofs_running << "\n\n\n\n";
    }
    int nspin0 = 1;
    if (GlobalV::NSPIN == 2)
        nspin0 = 2;
    // print occupation in istate.info
    ModuleIO::write_istate_info(this->pelec->ekb, this->pelec->wg, this->kv, &(GlobalC::Pkpoints));
    // compute density of states
    if (INPUT.out_dos)
    {
        ModuleIO::write_dos_pw(this->pelec->ekb,
                               this->pelec->wg,
                               this->kv,
                               INPUT.dos_edelta_ev,
                               INPUT.dos_scale,
                               INPUT.dos_sigma);

        if (nspin0 == 1)
        {
            GlobalV::ofs_running << " Fermi energy is " << this->pelec->eferm.ef << " Rydberg" << std::endl;
        }
        else if (nspin0 == 2)
        {
            GlobalV::ofs_running << " Fermi energy (spin = 1) is " << this->pelec->eferm.ef_up << " Rydberg"
                                 << std::endl;
            GlobalV::ofs_running << " Fermi energy (spin = 2) is " << this->pelec->eferm.ef_dw << " Rydberg"
                                 << std::endl;
        }
    }

    if (INPUT.out_band) // pengfei 2014-10-13
    {
        int nks = 0;
        if (nspin0 == 1)
        {
            nks = this->kv.nkstot;
        }
        else if (nspin0 == 2)
        {
            nks = this->kv.nkstot / 2;
        }
        for (int is = 0; is < nspin0; is++)
        {
            std::stringstream ss2;
            ss2 << GlobalV::global_out_dir << "BANDS_" << is + 1 << ".dat";
            GlobalV::ofs_running << "\n Output bands in file: " << ss2.str() << std::endl;
            ModuleIO::nscf_band(is,
                                ss2.str(),
                                nks,
                                GlobalV::NBANDS,
                                0.0,
                                this->pelec->ekb,
                                this->kv,
                                &(GlobalC::Pkpoints));
        }
    }

    if (GlobalV::BASIS_TYPE == "pw" && winput::out_spillage) // xiaohui add 2013-09-01
    {
        // std::cout << "\n Output Spillage Information : " << std::endl;
        //  calculate spillage value.
#ifdef __LCAO
// We are not goint to support lcao_in_paw until
// the obsolete GlobalC::hm is replaced by the
// refactored moeules (psi, hamilt, etc.)
/*
            if ( winput::out_spillage == 3)
            {
                GlobalV::BASIS_TYPE="pw";
                std::cout << " NLOCAL = " << GlobalV::NLOCAL << std::endl;

                for (int ik=0; ik<this->kv.nks; ik++)
                {
                    this->wf.wanf2[ik].create(GlobalV::NLOCAL, this->wf.npwx);
                    if(GlobalV::BASIS_TYPE=="pw")
                    {
                        std::cout << " ik=" << ik + 1 << std::endl;

                        GlobalV::BASIS_TYPE="lcao_in_pw";
                        this->wf.LCAO_in_pw_k(ik, this->wf.wanf2[ik]);
                        GlobalV::BASIS_TYPE="pw";
                    }
                }

                //Spillage sp;
                //sp.get_both(GlobalV::NBANDS, GlobalV::NLOCAL, this->wf.wanf2, this->wf.evc);
            }
*/
#endif

        // output overlap
        if (winput::out_spillage <= 2)
        {
            Numerical_Basis numerical_basis;
            numerical_basis.output_overlap(this->psi[0], this->sf, this->kv, this->pw_wfc);
            ModuleBase::GlobalFunc::DONE(GlobalV::ofs_running, "BASIS OVERLAP (Q and S) GENERATION.");
        }
    }

    if (this->wf.out_wfc_r == 1) // Peize Lin add 2021.11.21
    {
        ModuleIO::write_psi_r_1(this->psi[0], this->pw_wfc, "wfc_realspace", true, this->kv);
    }

    if (INPUT.cal_cond)
    {
        this->KG(INPUT.cond_nche, INPUT.cond_fwhm, INPUT.cond_wcut, INPUT.cond_dw, INPUT.cond_dt, this->pelec->wg);
    }
}

template <typename FPTYPE, typename Device>
void ESolver_KS_PW<FPTYPE, Device>::hamilt2estates(const FPTYPE ethr)
{
    if (this->phsol != nullptr)
    {
        hsolver::DiagoIterAssist<FPTYPE, Device>::need_subspace = false;
        hsolver::DiagoIterAssist<FPTYPE, Device>::PW_DIAG_THR = ethr;
        this->phsol->solve(this->p_hamilt, this->kspw_psi[0], this->pelec, GlobalV::KS_SOLVER, true);
    }
    else
    {
        ModuleBase::WARNING_QUIT("ESolver_KS_PW", "HSolver has not been initialed!");
    }
}

template <typename FPTYPE, typename Device>
void ESolver_KS_PW<FPTYPE, Device>::nscf()
{
    ModuleBase::TITLE("ESolver_KS_PW", "nscf");
    ModuleBase::timer::tick("ESolver_KS_PW", "nscf");

    this->beforescf(0);
    //========================================
    // diagonalization of the KS hamiltonian
    // =======================================
    FPTYPE diag_ethr = GlobalV::PW_DIAG_THR;
    if (diag_ethr - 1e-2 > -1e-5)
        diag_ethr = std::max(1e-13, 0.1 * std::min(1e-2, GlobalV::SCF_THR / GlobalV::nelec));
    GlobalV::ofs_running << " PW_DIAG_THR  = " << diag_ethr << std::endl;

    this->hamilt2estates(diag_ethr);
    this->pelec->calculate_weights();

    GlobalV::ofs_running << "\n End of Band Structure Calculation \n" << std::endl;

    for (int ik = 0; ik < this->kv.nks; ik++)
    {
        if (GlobalV::NSPIN == 2)
        {
            if (ik == 0)
                GlobalV::ofs_running << " spin up :" << std::endl;
            if (ik == (this->kv.nks / 2))
                GlobalV::ofs_running << " spin down :" << std::endl;
        }
        // out.printV3(GlobalV::ofs_running, this->kv.kvec_c[ik]);

        GlobalV::ofs_running << " k-points" << ik + 1 << "(" << this->kv.nkstot << "): " << this->kv.kvec_c[ik].x << " "
                             << this->kv.kvec_c[ik].y << " " << this->kv.kvec_c[ik].z << std::endl;

        for (int ib = 0; ib < GlobalV::NBANDS; ib++)
        {
            GlobalV::ofs_running << " spin" << this->kv.isk[ik] + 1 << "_final_band " << ib + 1 << " "
                                 << this->pelec->ekb(ik, ib) * ModuleBase::Ry_to_eV << " "
                                 << this->pelec->wg(ik, ib) * this->kv.nks << std::endl;
        }
        GlobalV::ofs_running << std::endl;
    }

    if (GlobalV::out_bandgap)
    {
        if (!GlobalV::TWO_EFERMI)
        {
            this->pelec->cal_bandgap();
            GlobalV::ofs_running << " E_bandgap " << this->pelec->bandgap * ModuleBase::Ry_to_eV << " eV" << std::endl;
        }
        else
        {
            this->pelec->cal_bandgap_updw();
            GlobalV::ofs_running << " E_bandgap_up " << this->pelec->bandgap_up * ModuleBase::Ry_to_eV << " eV"
                                 << std::endl;
            GlobalV::ofs_running << " E_bandgap_dw " << this->pelec->bandgap_dw * ModuleBase::Ry_to_eV << " eV"
                                 << std::endl;
        }
    }

    // add by jingan in 2018.11.7
    if (INPUT.towannier90)
    {
        toWannier90 myWannier(this->kv.nkstot, GlobalC::ucell.G);
        myWannier.init_wannier_pw(this->pelec->ekb, this->pw_rho, this->pw_wfc, this->pw_big, this->kv, this->psi);
    }

    //=======================================================
    // Do a Berry phase polarization calculation if required
    //=======================================================

    if (berryphase::berry_phase_flag && ModuleSymmetry::Symmetry::symm_flag != 1)
    {
        berryphase bp;
        bp.Macroscopic_polarization(this->pw_wfc->npwk_max, this->psi, this->pw_rho, this->pw_wfc, this->kv);
    }

    ModuleBase::timer::tick("ESolver_KS_PW", "nscf");
    return;
}

template class ESolver_KS_PW<float, psi::DEVICE_CPU>;
template class ESolver_KS_PW<double, psi::DEVICE_CPU>;
#if ((defined __CUDA) || (defined __ROCM))
template class ESolver_KS_PW<float, psi::DEVICE_GPU>;
template class ESolver_KS_PW<double, psi::DEVICE_GPU>;
#endif
} // namespace ModuleESolver
