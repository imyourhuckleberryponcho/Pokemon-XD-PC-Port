@echo off
rem Builds pbr_pack_moves_test.exe with MSVC into %1 (a scratch folder). Run from engine\formats\pbr.
call "%~dp0..\..\..\..\tools\msvc.bat" cl /nologo /std:c++17 /EHsc /O1 /MD /D_CRT_SECURE_NO_WARNINGS /Iinclude /Itests /Fo%1\ /Fe%1\t.exe tests\pbr_pack_moves_test.cpp pbr_pack_json.cpp pbr_pack_pbrdata.cpp pbr_pack_rules.cpp pbr_pack_moves.cpp pbr_batch.cpp pbr_to_xd.cpp pbr_conv_util.cpp pbr_conv_file.cpp pbr_eval.cpp hsd_read.cpp hsd_write.cpp model.cpp model_io.cpp model_anim.cpp sdr_read.cpp gx_tex.cpp pbr_gen4.cpp
