#!/bin/bash

oneTest () {
    cfg=test/$1
    correct=test/$2
    shift; shift
    ./grubby --bad-image-okay -c $cfg -o - $* | cmp $correct > /dev/null

    if [ $? != 0 ]; then 
	echo -------------------------------------------------------------
	echo FAILURE: $cfg $correct $*
	./grubby --bad-image-okay -c $cfg -o - $* | diff -u $correct -; 
    fi 
}

echo "Parse/write comparison..."
for n in $(cd test; echo grub.[0-9]*); do
    oneTest $n $n --remove-kernel 1234
done

for n in $(cd test; echo lilo.[0-9]*); do
    oneTest $n $n --lilo --remove-kernel 1234
done

echo "Permission preservation..."
cp test/grub.1 grub-test
chmod 0614 grub-test 
touch 01010101 grub-test
time=$(ls -l grub-test | awk '{ print $6 " " $7 " "$8}')
perm=$(ls -l grub-test | awk '{print $1}')
./grubby ./grubby --add-kernel bar --title title -c grub-test 
newtime=$(ls -l grub-test | awk '{ print $6 " " $7 " "$8}')
newperm=$(ls -l grub-test | awk '{print $1}')
if [ "$time" == "$newtime" -o "$perm" != "$newperm" ]; then
    echo "  failed ($p)"; 
fi
rm -f grub-test

cp test/lilo.1 lilo-test
chmod 0614 lilo-test 
touch 01010101 lilo-test
time=$(ls -l lilo-test | awk '{ print $6 " " $7 " "$8}')
perm=$(ls -l lilo-test | awk '{print $1}')
./grubby ./grubby --lilo --add-kernel bar --title title -c lilo-test 
newtime=$(ls -l lilo-test | awk '{ print $6 " " $7 " "$8}')
newperm=$(ls -l lilo-test | awk '{print $1}')
if [ "$time" == "$newtime" -o "$perm" != "$newperm" ]; then
    echo "  failed ($p)"; 
fi
rm -f lilo-test

echo "GRUB default directive..."
oneTest grub.1 results/default/g1.1 --boot-filesystem=/boot --add-kernel /boot/new-kernel --title Some_Title 
oneTest grub.1 results/default/g1.2 --boot-filesystem=/boot --add-kernel /boot/new-kernel --title Some_Title --make-default
oneTest grub.3 results/default/g3.1 --boot-filesystem=/boot --set-default=/boot/vmlinuz-2.4.7-2
oneTest grub.3 results/default/g3.2 --boot-filesystem=/boot --set-default=/boot/vmlinuz-2.4.7-2smp
oneTest grub.4 results/default/g4.1 --boot-filesystem=/ --set-default=/boot/vmlinuz-2.4.7-ac3 --remove-kernel /boot/vmlinuz-2.4.7-2.5
oneTest grub.4 results/default/g4.2 --boot-filesystem=/ --set-default=/boot/vmlinuz-2.4.7-ac3 --remove-kernel /boot/vmlinuz-2.4.7-2.5 --add-kernel=/boot/new-kernel --copy-default --title New_Title
oneTest grub.6 results/default/g6.1 --remove-kernel=/boot/vmlinuz-2.4.7-2.9 --boot-filesystem=/

echo "GRUB fallback directive..."
oneTest grub.5 results/fallback/g5.1 --remove-kernel=/boot/vmlinuz-2.4.7-ac3 \
    --boot-filesystem=/
oneTest grub.5 results/fallback/g5.2 --remove-kernel=/boot/vmlinuz-2.4.7-2.5 \
    --boot-filesystem=/
oneTest grub.5 results/fallback/g5.3 --remove-kernel=/boot/vmlinuz-2.4.7-2.5 \
    --boot-filesystem=/ --copy-default --add-kernel=/boot/new-kernel \
    --title="Some_Title"

echo "GRUB kernel argument handling..."
oneTest grub.1 results/args/g1.1 --boot-filesystem=/boot \
    --add-kernel=/boot/foo --title=some_title --args="1234" --copy-default
oneTest grub.1 results/args/g1.2 --boot-filesystem=/boot \
    --add-kernel=/boot/foo --title=some_title --args="1234" 


