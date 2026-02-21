echo "Creating the Library"
echo "[Not building just copying files around]"
echo ""
echo "Creating the directory"
rm -rf build/vesper
mkdir build/vesper
echo "Moving headers"
cp -r include build/vesper
echo "Moving vesper.h"
mkdir build/vesper/vesper
cp src/vesper.h build/vesper/vesper
echo "Moving libvesper.a"
cp build/libvesper.a build/vesper
echo ""
echo "Library has been succesfully created in the build directory"