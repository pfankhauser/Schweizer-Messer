// This file automatically generated by create_export_module.py
#include "../NumpyEigenConverter.hpp"


void import_16_04_double()
{
	// Without this import, the converter will segfault
	import_array();
	NumpyEigenConverter<Eigen::Matrix< double, 16, 4 > >::register_converter();
}
