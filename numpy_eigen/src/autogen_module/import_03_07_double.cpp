// This file automatically generated by create_export_module.py
#include "../NumpyEigenConverter.hpp"


void import_03_07_double()
{
	// Without this import, the converter will segfault
	import_array();
	NumpyEigenConverter<Eigen::Matrix< double, 3, 7 > >::register_converter();
}
