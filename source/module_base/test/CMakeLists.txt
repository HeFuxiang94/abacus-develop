remove_definitions(-D__MPI)
AddTest(
  TARGET base_blas_connector
  LIBS ${math_libs}
  SOURCES blas_connector_test.cpp
)
AddTest(
  TARGET base_timer
  SOURCES timer_test.cpp ../timer.cpp
)
AddTest(
  TARGET base_tool_quit
  SOURCES tool_quit_test.cpp ../tool_quit.cpp ../global_variable.cpp ../global_file.cpp ../global_function.cpp ../memory.cpp ../timer.cpp
)
AddTest(
  TARGET base_tool_check
  SOURCES tool_check_test.cpp ../tool_check.cpp ../tool_quit.cpp ../global_variable.cpp ../global_file.cpp ../global_function.cpp ../memory.cpp ../timer.cpp
)
AddTest(
  TARGET base_memory
  SOURCES memory_test.cpp ../memory.cpp
)
ADDTest(
  TARGET base_global_function
  LIBS ${math_libs}
  SOURCES global_function_test.cpp ../global_function.cpp ../tool_quit.cpp ../global_variable.cpp ../global_file.cpp ../memory.cpp ../timer.cpp
)
AddTest(
  TARGET base_vector3
  SOURCES vector3_test.cpp
)
AddTest(
  TARGET base_matrix3
  LIBS ${math_libs}
  SOURCES matrix3_test.cpp ../matrix3.cpp ../matrix.cpp ../tool_quit.cpp ../global_variable.cpp ../global_file.cpp ../global_function.cpp ../memory.cpp ../timer.cpp
)
AddTest(
  TARGET base_intarray
  SOURCES intarray_test.cpp ../intarray.cpp
)
AddTest(
  TARGET base_realarray
  SOURCES realarray_test.cpp ../realarray.cpp
)
AddTest(
 TARGET base_matrix
  LIBS ${math_libs}
  SOURCES matrix_test.cpp ../matrix.cpp ../tool_quit.cpp ../global_variable.cpp ../global_file.cpp ../global_function.cpp ../memory.cpp ../timer.cpp
)
AddTest(
  TARGET base_complexarray
  SOURCES complexarray_test.cpp ../complexarray.cpp ../tool_quit.cpp ../global_variable.cpp ../global_file.cpp ../global_function.cpp ../memory.cpp ../timer.cpp
)
AddTest(
  TARGET base_complexmatrix
  LIBS ${math_libs}
  SOURCES complexmatrix_test.cpp ../complexmatrix.cpp ../matrix.cpp
)
AddTest(
  TARGET base_integral
  SOURCES math_integral_test.cpp ../math_integral.cpp
)
AddTest(
  TARGET base_sph_bessel_recursive
  SOURCES sph_bessel_recursive_test.cpp ../sph_bessel_recursive-d1.cpp ../sph_bessel_recursive-d2.cpp
)
AddTest(
  TARGET base_ylmreal
  LIBS ${math_libs} device
  SOURCES math_ylmreal_test.cpp ../math_ylmreal.cpp ../complexmatrix.cpp ../global_variable.cpp ../ylm.cpp ../realarray.cpp ../timer.cpp ../matrix.cpp ../vector3.h
          ../parallel_reduce.cpp ../parallel_global.cpp ../parallel_common.cpp
          ../memory.cpp ../libm/branred.cpp ../libm/sincos.cpp
)
AddTest(
  TARGET base_math_sphbes
  SOURCES math_sphbes_test.cpp ../math_sphbes.cpp ../timer.cpp
)
AddTest(
  TARGET base_mathzone
  LIBS ${math_libs}
  SOURCES mathzone_test.cpp ../matrix3.cpp ../matrix.cpp ../tool_quit.cpp ../global_variable.cpp ../global_file.cpp ../global_function.cpp ../memory.cpp ../timer.cpp
)
AddTest(
  TARGET base_mathzone_add1
  LIBS ${math_libs}
  SOURCES mathzone_add1_test.cpp ../mathzone_add1.cpp ../math_sphbes.cpp ../matrix3.cpp ../matrix.cpp ../tool_quit.cpp ../global_variable.cpp ../global_file.cpp ../global_function.cpp ../memory.cpp ../timer.cpp
)
AddTest(
  TARGET base_math_polyint
  SOURCES math_polyint_test.cpp ../math_polyint.cpp ../realarray.cpp ../timer.cpp
)
AddTest(
  TARGET base_gram_schmidt_orth
  LIBS ${math_libs}
  SOURCES gram_schmidt_orth_test.cpp ../gram_schmidt_orth.h ../gram_schmidt_orth-inl.h ../global_function.h ../math_integral.cpp
)
AddTest(
  TARGET base_math_bspline
  SOURCES math_bspline_test.cpp ../math_bspline.cpp
)
AddTest(
  TARGET base_inverse_matrix
  LIBS ${math_libs}
  SOURCES inverse_matrix_test.cpp ../inverse_matrix.cpp ../complexmatrix.cpp ../matrix.cpp ../timer.cpp
)
AddTest(
  TARGET base_mymath
  SOURCES mymath_test.cpp ../mymath.cpp ../timer.cpp
)
AddTest(
  TARGET base_container
  SOURCES container_operator_test.cpp ../container_operator.h
)

AddTest(
  TARGET base_math_chebyshev
  LIBS ${math_libs}
  SOURCES math_chebyshev_test.cpp ../math_chebyshev.cpp ../tool_quit.cpp ../global_variable.cpp ../timer.cpp ../global_file.cpp ../global_function.cpp ../memory.cpp
)

AddTest(
  TARGET base_opt_CG
  SOURCES opt_CG_test.cpp opt_test_tools.cpp ../opt_CG.cpp ../opt_DCsrch.cpp ../global_variable.cpp ../parallel_reduce.cpp
)

AddTest(
  TARGET base_opt_TN
  SOURCES opt_TN_test.cpp opt_test_tools.cpp ../opt_CG.cpp ../opt_DCsrch.cpp ../global_variable.cpp ../parallel_reduce.cpp
)

AddTest(
  TARGET base_ylm
  SOURCES ylm_test.cpp  ../ylm.cpp  ../timer.cpp  ../tool_quit.cpp ../global_variable.cpp ../global_file.cpp ../global_function.cpp ../memory.cpp
)

AddTest(
  TARGET base_abfs-vector3_order
  SOURCES abfs-vector3_order_test.cpp ../abfs-vector3_order.cpp
)

AddTest(
  TARGET base_global_file
  SOURCES global_file_test.cpp ../global_file.cpp ../global_function.cpp ../tool_quit.cpp ../global_variable.cpp ../memory.cpp ../timer.cpp
)

AddTest(
  TARGET base_tool_title
  SOURCES tool_title_test.cpp ../tool_title.cpp  ../global_variable.cpp ../global_function.cpp ../timer.cpp ../tool_quit.cpp  ../global_file.cpp ../memory.cpp
)

AddTest(
  TARGET base_element_basis_index
  SOURCES element_basis_index_test.cpp ../element_basis_index.cpp
)

AddTest(
  TARGET base_tool_threading
  SOURCES tool_threading_test.cpp ../tool_threading.h
)

AddTest(
  TARGET spherical_bessel_transformer
  SOURCES spherical_bessel_transformer_test.cpp ../spherical_bessel_transformer.cpp
  LIBS ${math_libs}
)
