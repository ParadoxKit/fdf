@echo off

if NOT exist "_PROJ" ( mkdir "_PROJ" )

pushd "_PROJ"
cmake .. -DFDF_USE_CPP_MODULES=ON
popd
