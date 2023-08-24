cd $PSScriptRoot

# Rename .py.in files to .py for Windows
cp ./setup.py.in ./setup.py
cp ./src/libmapper/mapper.py.in ./src/libmapper/mapper.py

# Replace version env variable with actual version
$versionString = git describe --tags
$versionString = $versionString -replace '([0-9.]+)-([0-9]+)-([a-z0-9]+)', '$1.$2+$3'
(gc ./src/libmapper/mapper.py) -replace '__version__ = ''@PACKAGE_VERSION@''', "__version__ = '$versionString'" | Out-File -encoding ASCII ./src/libmapper/mapper.py

# Copy dlls to python bindings folder
cp -v ../../dist/libmapper.dll ./src/libmapper/libmapper.dll
cp -v ../../dist/liblo.dll ./src/libmapper/liblo.dll
cp -v ../../dist/zlib.dll ./src/libmapper/zlib.dll

# Build python wheel
pip install wheel
python setup.py bdist_wheel

Write-Host "Done! Python wheel located in the bindings/python/dist/ folder"
