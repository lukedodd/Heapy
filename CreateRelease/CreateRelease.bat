call "C:\Program Files (x86)\Microsoft Visual Studio 10.0\VC\bin\vcvars32.bat"
devenv ..\Heapy.sln /Rebuild "Release|Win32"
devenv ..\Heapy.sln /Rebuild "Release|x64"

rd ..\dist /s/q
mkdir ..\dist
copy ..\Release\*.exe ..\dist
copy ..\Release\*.dll ..\dist
copy ..\Release\TestApplication_Win32.pdb ..\dist
copy ..\Release\TestApplication_x64.pdb ..\dist
copy Readme.txt ..\dist
copy ProfileTestApplication_Win32.bat ..\dist
copy ProfileTestApplication_x64.bat ..\dist
