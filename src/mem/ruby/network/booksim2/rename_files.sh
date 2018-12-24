#!/bin/sh

hppfiles=`ls -1 *.hpp`
for hpp in $hppfiles
do
    mv "$hpp" "$(basename "$hpp" .hpp).hh"
done

cppfiles=`ls -1 *.cpp`
for cpp in $cppfiles
do
    mv "$cpp" "$(basename "$cpp" .cpp).cc"
done
