#!/bin/bash

echo "Parse/write comparison..."
for n in test/grub.[0-9]*; do 
    ./grubby --remove-kernel 1234 -c $n -o - | cmp $n; 
    if [ $? != 0 ]; then 
	./grubby --remove-kernel 1234 -c $n -o - | diff -u - $n; 
    fi 
done
echo "Permission preservation..."
cp test/grub.1 grub-test
chmod 0614 grub-test 
./grubby ./grubby --add-kernel bar --title title -c grub-test 
p=$(ls -l grub-test | awk '{print $1}')
if [  $p != '-rw---xr--' ]; then 
    echo "  failed ($p)"; 
fi
rm -f grub-test
