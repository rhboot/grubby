#!/bin/bash

oneTest () {
    mode=$1
    cfg=test/$2
    correct=test/results/$3
    shift; shift; shift
    ./grubby $mode --bad-image-okay -c $cfg -o - "$@" | cmp $correct > /dev/null

    if [ $? != 0 ]; then 
	echo -------------------------------------------------------------
	echo FAILURE: $cfg $correct "$@"
	echo -n ./grubby $mode --bad-image-okay -c $cfg -o - 
	for arg in "$@"; do
	    echo -n " \"$arg\""
	done
	echo ""
	./grubby $mode --bad-image-okay -c $cfg -o - "$@" | diff -u $correct -; 
    fi 
}

liloTest() {
    oneTest --lilo "$@"
}

grubTest() {
    oneTest --grub "$@"
}

echo "Parse/write comparison..."
for n in $(cd test; echo grub.[0-9]*); do
    grubTest $n ../$n --remove-kernel 1234
done

for n in $(cd test; echo lilo.[0-9]*); do
    liloTest $n ../$n --remove-kernel 1234
done

echo "Permission preservation..."
cp test/grub.1 grub-test
chmod 0614 grub-test 
touch 01010101 grub-test
time=$(ls -l grub-test | awk '{ print $6 " " $7 " "$8}')
perm=$(ls -l grub-test | awk '{print $1}')
./grubby --grub --add-kernel bar --title title -c grub-test 
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
./grubby --lilo --add-kernel bar --title title -c lilo-test 
newtime=$(ls -l lilo-test | awk '{ print $6 " " $7 " "$8}')
newperm=$(ls -l lilo-test | awk '{print $1}')
if [ "$time" == "$newtime" -o "$perm" != "$newperm" ]; then
    echo "  failed ($p)"; 
fi
rm -f lilo-test

echo "GRUB default directive..."
grubTest grub.1 default/g1.1 --boot-filesystem=/boot --add-kernel /boot/new-kernel --title Some_Title 
grubTest grub.1 default/g1.2 --boot-filesystem=/boot --add-kernel /boot/new-kernel --title Some_Title --make-default
grubTest grub.3 default/g3.1 --boot-filesystem=/boot --set-default=/boot/vmlinuz-2.4.7-2
grubTest grub.3 default/g3.2 --boot-filesystem=/boot --set-default=/boot/vmlinuz-2.4.7-2smp
grubTest grub.4 default/g4.1 --boot-filesystem=/ --set-default=/boot/vmlinuz-2.4.7-ac3 --remove-kernel /boot/vmlinuz-2.4.7-2.5
grubTest grub.4 default/g4.2 --boot-filesystem=/ --set-default=/boot/vmlinuz-2.4.7-ac3 --remove-kernel /boot/vmlinuz-2.4.7-2.5 --add-kernel=/boot/new-kernel --copy-default --title New_Title
grubTest grub.6 default/g6.1 --remove-kernel=/boot/vmlinuz-2.4.7-2.9 --boot-filesystem=/

echo "LILO default directive..."
liloTest lilo.1 default/l1.1 --set-default=/boot/vmlinuz-2.4.18-4
liloTest lilo.1 default/l1.2 --remove-kernel=/boot/vmlinuz-2.4.18-4smp
liloTest lilo.1 default/l1.3 --add-kernel /boot/kernel --title label \
    --copy-default
liloTest lilo.1 default/l1.4 --add-kernel /boot/kernel --title label \
    --copy-default --make-default

echo "GRUB fallback directive..."
grubTest grub.5 fallback/g5.1 --remove-kernel=/boot/vmlinuz-2.4.7-ac3 \
    --boot-filesystem=/
grubTest grub.5 fallback/g5.2 --remove-kernel=/boot/vmlinuz-2.4.7-2.5 \
    --boot-filesystem=/
grubTest grub.5 fallback/g5.3 --remove-kernel=/boot/vmlinuz-2.4.7-2.5 \
    --boot-filesystem=/ --copy-default --add-kernel=/boot/new-kernel \
    --title="Some_Title"

echo "GRUB new kernel argument handling..."
grubTest grub.1 args/g1.1 --boot-filesystem=/boot \
    --add-kernel=/boot/foo --title=some_title --args="1234" --copy-default
grubTest grub.1 args/g1.2 --boot-filesystem=/boot \
    --add-kernel=/boot/foo --title=some_title --args="1234" 

echo "GRUB remove directive..."
grubTest grub.7 remove/g7.1 --boot-filesystem=/ \
    --remove-kernel=/boot/vmlinuz-2.4.7-2.5
grubTest grub.3 remove/g3.1 --boot-filesystem=/ \
    --remove-kernel=DEFAULT

echo "GRUB update kernel argument handling..."
grubTest grub.1 updargs/g1.1 --update-kernel=DEFAULT --args="root=/dev/hda1"
grubTest grub.1 updargs/g1.2 --update-kernel=DEFAULT \
    --args="root=/dev/hda1 hda=ide-scsi root=/dev/hda2"
grubTest grub.3 updargs/g3.1 --update-kernel=DEFAULT --args "hdd=notide-scsi"
grubTest grub.3 updargs/g3.2 --update-kernel=DEFAULT \
    --args "hdd=notide-scsi root=/dev/hdd1"
grubTest grub.3 updargs/g3.2 --update-kernel=DEFAULT \
    --args "root=/dev/hdd1 hdd=notide-scsi"
grubTest grub.3 updargs/g3.4 --update-kernel=ALL --remove-args="hdd"
grubTest grub.3 updargs/g3.4 --update-kernel=ALL --remove-args="hdd=ide-scsi"
grubTest grub.3 updargs/g3.4 --update-kernel=ALL --remove-args="hdd=foobar"
grubTest grub.3 updargs/g3.7 --update-kernel=ALL \
    --remove-args="hdd root ro"
grubTest grub.7 updargs/g7.2 --boot-filesystem=/    \
    --update-kernel=ALL --args "hde=ide-scsi"
grubTest grub.7 updargs/g7.2 --boot-filesystem=/    \
    --update-kernel=ALL --args "hde=ide-scsi"
grubTest grub.7 updargs/g7.3 --boot-filesystem=/    \
    --update-kernel=DEFAULT --args "hde=ide-scsi"

echo "LILO update kernel argument handling..."
liloTest lilo.1 updargs/l1.1 --update-kernel=/boot/vmlinuz-2.4.18-4 \
    --args="root=/dev/md1"
liloTest lilo.1 updargs/l1.2 --update-kernel=/boot/vmlinuz-2.4.18-4smp \
    --args="root=LABEL=foo"
liloTest lilo.1 updargs/l1.3 --update-kernel=DEFAULT --args="foo"
liloTest lilo.1 updargs/l1.4 --update-kernel=ALL --args="foo bar root=/dev/md1"
liloTest lilo.1 updargs/l1.4 --update-kernel=ALL --args="foo root=/dev/md1 bar"
liloTest lilo.1 updargs/l1.6 --update-kernel=ALL --args="foo root=LABEL=/ bar"
liloTest lilo.3 updargs/l3.1 --update-kernel=/boot/vmlinuz-2.4.18-4 \
    --remove-args="hda"
liloTest lilo.3 updargs/l3.2 --update-kernel=ALL \
    --remove-args="single" --args "root=/dev/hda2"
