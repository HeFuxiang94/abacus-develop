#include "esolver_fp.h"

#include "module_base/global_variable.h"
#include "module_hamilt_pw/hamilt_pwdft/global.h"
#include "module_io/input.h"
namespace ModuleESolver
{   ESolver_FP::ESolver_FP()
    {
        // pw_rho = new ModuleBase::PW_Basis();
        
        pw_rho = new ModulePW::PW_Basis_Big(GlobalV::device_flag, GlobalV::precision_flag);

        //temporary, it will be removed
        pw_big = static_cast<ModulePW::PW_Basis_Big*>(pw_rho);
        pw_big->setbxyz(INPUT.bx, INPUT.by, INPUT.bz);
        sf.set(INPUT.nbspline);

        this->symm.epsilon = INPUT.symmetry_prec;
    }
    ESolver_FP::~ESolver_FP()
    {
        if (this->psi != nullptr)
        {
            delete psi;
        }
        if (this->psid != nullptr)
        {
            delete psid;
        }
        delete pw_rho;
        delete this->pelec;
    }
    void ESolver_FP::Init(Input& inp, UnitCell& cell)
    {
        cell.read_pseudo(GlobalV::ofs_running);

#ifdef __MPI
            this->pw_rho->initmpi(GlobalV::NPROC_IN_POOL, GlobalV::RANK_IN_POOL, POOL_WORLD);
#endif
        if (this->classname == "ESolver_OF") this->pw_rho->setfullpw(inp.of_full_pw, inp.of_full_pw_dim);
        // Initalize the plane wave basis set
        if (inp.nx * inp.ny * inp.nz == 0)
            this->pw_rho->initgrids(inp.ref_cell_factor * cell.lat0, cell.latvec, inp.ecutrho);
	    else
            this->pw_rho->initgrids(inp.ref_cell_factor * cell.lat0, cell.latvec, inp.nx, inp.ny, inp.nz);
        
        this->pw_rho->initparameters(false, inp.ecutrho);
        this->pw_rho->setuptransform();
        this->pw_rho->collect_local_pw(); 
        this->pw_rho->collect_uniqgg();
        this->print_rhofft(inp, GlobalV::ofs_running);
        
    }

    void ESolver_FP::init_after_vc(Input& inp, UnitCell& cell)
    {
        ModuleBase::TITLE("ESolver_FP", "init_after_vc");

        if (GlobalV::md_prec_level == 0)
        {
            //only G-vector and K-vector are changed due to the change of lattice vector
            //FFT grids do not change!!
            pw_rho->initgrids(cell.lat0, cell.latvec, pw_rho->nx, pw_rho->ny, pw_rho->nz);
            pw_rho->collect_local_pw(); 
            pw_rho->collect_uniqgg();

            GlobalC::ppcell.init_vloc(GlobalC::ppcell.vloc, pw_rho);
            ModuleBase::GlobalFunc::DONE(GlobalV::ofs_running,"LOCAL POTENTIAL");
        }
        else if (GlobalV::md_prec_level == 1)
        {
            GlobalC::ppcell.init_vloc(GlobalC::ppcell.vloc, pw_rho);
            ModuleBase::GlobalFunc::DONE(GlobalV::ofs_running,"LOCAL POTENTIAL");
        }
        else if (GlobalV::md_prec_level == 2)
        {
            if (inp.nx * inp.ny * inp.nz == 0)
                this->pw_rho->initgrids(cell.lat0, cell.latvec, inp.ecutrho);
            else
                this->pw_rho->initgrids(cell.lat0, cell.latvec, inp.nx, inp.ny, inp.nz);

            this->pw_rho->initparameters(false, inp.ecutrho);
            this->pw_rho->setuptransform();
            this->pw_rho->collect_local_pw(); 
            this->pw_rho->collect_uniqgg();
        }
        this->pelec->omega = GlobalC::ucell.omega;

        if(ModuleSymmetry::Symmetry::symm_flag == 1)
        {
            symm.analy_sys(cell, GlobalV::ofs_running);
            ModuleBase::GlobalFunc::DONE(GlobalV::ofs_running, "SYMMETRY");
        }

        kv.set_after_vc(symm, GlobalV::global_kpoint_card, GlobalV::NSPIN, cell.G, cell.latvec);
        ModuleBase::GlobalFunc::DONE(GlobalV::ofs_running, "INIT K-POINTS");
    }

    void ESolver_FP::print_rhofft(Input&inp, ofstream &ofs)
    {
        std::cout << " UNIFORM GRID DIM     : " << pw_rho->nx << " * " << pw_rho->ny << " * " << pw_rho->nz
                  << std::endl;
        std::cout << " UNIFORM GRID DIM(BIG): " << pw_big->nbx << " * " << pw_big->nby << " * " << pw_big->nbz
                  << std::endl;

        ofs << "\n\n\n\n";
	    ofs << " >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>" << std::endl;
	    ofs << " |                                                                    |" << std::endl;
	    ofs << " | Setup plane waves of charge/potential:                             |" << std::endl;
	    ofs << " | Use the energy cutoff and the lattice vectors to generate the      |" << std::endl;
	    ofs << " | dimensions of FFT grid. The number of FFT grid on each processor   |" << std::endl;
	    ofs << " | is 'nrxx'. The number of plane wave basis in reciprocal space is   |" << std::endl;
	    ofs << " | different for charege/potential and wave functions. We also set    |" << std::endl;
	    ofs << " | the 'sticks' for the parallel of FFT. The number of plane waves    |" << std::endl;
	    ofs << " | is 'npw' in each processor.                                        |" << std::endl;
	    ofs << " <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<" << std::endl;
	    ofs << "\n\n\n\n";
	    ofs << "\n SETUP THE PLANE WAVE BASIS" << std::endl;
        double ecut = INPUT.ecutrho;
        if(inp.nx * inp.ny * inp.nz > 0)
        {
            ecut = this->pw_rho->gridecut_lat * this->pw_rho->tpiba2;
            ofs << "use input fft dimensions for wave functions." << std::endl;
            ofs << "calculate energy cutoff from nx, ny, nz:" << std::endl;

        }
        ModuleBase::GlobalFunc::OUT(ofs,"energy cutoff for charge/potential (unit:Ry)", ecut);
            
	    ModuleBase::GlobalFunc::OUT(ofs,"fft grid for charge/potential", this->pw_rho->nx,this->pw_rho->ny,this->pw_rho->nz);
        ModuleBase::GlobalFunc::OUT(ofs, "fft grid division", pw_big->bx, pw_big->by, pw_big->bz);
        ModuleBase::GlobalFunc::OUT(ofs, "big fft grid for charge/potential", pw_big->nbx, pw_big->nby, pw_big->nbz);
        ModuleBase::GlobalFunc::OUT(ofs, "nbxx", pw_big->nbxx);
        ModuleBase::GlobalFunc::OUT(ofs, "nrxx", this->pw_rho->nrxx);

        ofs << "\n SETUP PLANE WAVES FOR CHARGE/POTENTIAL" << std::endl;
        ModuleBase::GlobalFunc::OUT(ofs,"number of plane waves",this->pw_rho->npwtot);
	    ModuleBase::GlobalFunc::OUT(ofs,"number of sticks", this->pw_rho->nstot);

        ofs << "\n PARALLEL PW FOR CHARGE/POTENTIAL" << std::endl;
        ofs <<" "<< std::setw(8)  << "PROC"<< std::setw(15) << "COLUMNS(POT)"<< std::setw(15) << "PW" << std::endl;
        for (int i = 0; i < GlobalV::NPROC_IN_POOL ; ++i)
        {
            ofs <<" "<<std::setw(8)<< i+1 << std::setw(15) << this->pw_rho->nst_per[i] << std::setw(15) << this->pw_rho->npw_per[i] << std::endl;
        }
        ofs << " --------------- sum -------------------" << std::endl;
        ofs << " " << std::setw(8)  << GlobalV::NPROC_IN_POOL << std::setw(15) << this->pw_rho->nstot << std::setw(15) << this->pw_rho->npwtot << std::endl;
        
        ModuleBase::GlobalFunc::OUT(ofs,"number of |g|", this->pw_rho->ngg);
        ModuleBase::GlobalFunc::OUT(ofs,"max |g|", this->pw_rho->gg_uniq[ this->pw_rho->ngg-1]);
	    ModuleBase::GlobalFunc::OUT(ofs,"min |g|", this->pw_rho->gg_uniq[0]);
    }
}