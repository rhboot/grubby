#!/bin/bash
#
# test.sh -- grubby regression tests
#
# Copyright 2007-2008 Red Hat, Inc.  All rights reserved.
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.
#

if [ -n "$TOPDIR" ]; then
    LD_LIBRARY_PATH="$TOPDIR/nash:$TOPDIR/bdevid:/usr/lib:/lib"
    export LD_LIBRARY_PATH
fi

#----------------------------------------------------------------------
# Global vars
#----------------------------------------------------------------------

cmd=${0##*/}
opt_bootloader=*
opt_verbose=false
read -d '' usage <<EOT
usage: test.sh [ -hv ]

    -b B   --bootloader=B  Test bootloader B instead of all
    -h     --help          Show this help message
    -v     --verbose       Verbose output
EOT
declare -i pass=0 fail=0
testing=

#----------------------------------------------------------------------
# Functions
#----------------------------------------------------------------------

oneTest() {
    typeset mode=$1 cfg=test/$2 correct=test/results/$3
    shift 3

    echo "$testing ... $mode $cfg $correct"
    runme=( ./grubby "$mode" --bad-image-okay -c "$cfg" -o - "$@" )
    if "${runme[@]}" | cmp "$correct" > /dev/null; then
	(( pass++ ))
	if $opt_verbose; then
	    echo -------------------------------------------------------------
	    echo -n "PASS: "
	    printf "%q " "${runme[@]}"; echo
	    "${runme[@]}" | diff -U30 "$cfg" -
	    echo
	fi
    else
	(( fail++ ))
	echo -------------------------------------------------------------
	echo -n "FAIL: "
	printf "%q " "${runme[@]}"; echo
	"${runme[@]}" | diff -U30 "$correct" -
	echo
    fi 
}

# generate convenience functions
for b in $(./grubby --help | \
	sed -n 's/^.*--\([^ ]*\) *configure \1 bootloader$/\1/p'); do
    eval "${b}Test() { [[ \"$b\" == \$opt_bootloader ]] && oneTest --$b \"\$@\"; }"
done

#----------------------------------------------------------------------
# Main
#----------------------------------------------------------------------

# Use /usr/bin/getopt which supports GNU-style long options
args=$(getopt -o b:hv --long bootloader,help,verbose -n "$cmd" -- "$@") || exit
eval set -- "$args"
while true; do
    case $1 in
	-b|--bootloader) opt_bootloader=$2; shift 2 ;;
	-h|--help) echo "$usage"; exit 0 ;;
	-v|--verbose) opt_verbose=true; shift ;;
        --) shift; break ;;
        *) echo "failed to process cmdline args" >&2; exit 1 ;;
    esac
done

export MALLOC_CHECK_=2

testing="Parse/write comparison"
for n in test/*.[0-9]*; do
    n=${n#*/}	# remove test/
    b=${n%.*}	# remove suffix
    [[ $b == $opt_bootloader ]] || continue
    ${b}Test $n ../$n --remove-kernel 1234
done

testing="Permission preservation"
unset b
for n in test/*.[0-9]*; do
    n=${n#*/}	# remove test/
    [[ ${n%.*} == "$b" ]] && continue
    b=${n%.*}	# remove suffix
    [[ $b == $opt_bootloader ]] || continue

    echo "$testing ... --$b"

    cp test/$n ${b}-test
    chmod 0614 ${b}-test 
    touch -t 200301010101.00 ${b}-test
    time=$(ls -l ${b}-test | awk '{ print $6 " " $7 " "$8}')
    perm=$(ls -l ${b}-test | awk '{print $1}')
    ./grubby --${b} --add-kernel bar --title title -c ${b}-test 
    if [[ $? != 0 ]]; then
	echo "  FAIL (grubby returned non-zero)"
	(( fail++ ))
    elif newtime=$(ls -l ${b}-test | awk '{ print $6 " " $7 " "$8}') && \
	newperm=$(ls -l ${b}-test | awk '{print $1}') && \
	[[ $time == "$newtime" || $perm != "$newperm" ]]
    then
	echo "  FAIL ($perm $newperm)"; 
	(( fail++ ))
    else
	(( pass++ ))
    fi
    rm -f ${b}-test
done

testing="Following symlinks"
unset b
for n in test/*.[0-9]*; do
    n=${n#*/}	# remove test/
    [[ ${n%.*} == "$b" ]] && continue
    b=${n%.*}	# remove suffix
    [[ $b == $opt_bootloader ]] || continue

    echo "$testing ... --$b"

    cp test/${b}.1 ${b}-test
    ln -s ./${b}-test mytest
    ./grubby --${b} --add-kernel bar --title title -c mytest
    if [[ $? != 0 ]]; then
	echo "  failed (grubby returned non-zero)"
	(( fail++ ))
    elif [[ ! -L mytest ]]; then
	echo "  failed (not a symlink)"
	(( fail++ ))
    elif target=$(readlink mytest) && [[ $target != "./${b}-test" ]]; then
	echo "  failed (wrong target)"
	(( fail++ ))
    else
	(( pass++ ))
    fi
    rm -f ${b}-test mytest
done

testing="GRUB default directive"
grubTest grub.1 default/g1.1 --boot-filesystem=/boot --add-kernel /boot/new-kernel --title Some_Title 
grubTest grub.1 default/g1.2 --boot-filesystem=/boot --add-kernel /boot/new-kernel --title Some_Title --make-default
grubTest grub.3 default/g3.1 --boot-filesystem=/boot --set-default=/boot/vmlinuz-2.4.7-2
grubTest grub.3 default/g3.2 --boot-filesystem=/boot --set-default=/boot/vmlinuz-2.4.7-2smp
grubTest grub.4 default/g4.1 --boot-filesystem=/ --set-default=/boot/vmlinuz-2.4.7-ac3 --remove-kernel /boot/vmlinuz-2.4.7-2.5
grubTest grub.4 default/g4.2 --boot-filesystem=/ --set-default=/boot/vmlinuz-2.4.7-ac3 --remove-kernel /boot/vmlinuz-2.4.7-2.5 --add-kernel=/boot/new-kernel --copy-default --title New_Title
grubTest grub.6 default/g6.1 --remove-kernel=/boot/vmlinuz-2.4.7-2.9 --boot-filesystem=/

testing="LILO default directive"
liloTest lilo.1 default/l1.1 --set-default=/boot/vmlinuz-2.4.18-4
liloTest lilo.1 default/l1.2 --remove-kernel=/boot/vmlinuz-2.4.18-4smp
liloTest lilo.1 default/l1.3 --add-kernel /boot/kernel --title label \
    --copy-default
liloTest lilo.1 default/l1.4 --add-kernel /boot/kernel --title label \
    --copy-default --make-default

testing="Z/IPL default directive"
ziplTest zipl.1 default/z1.1 --add-kernel /boot/new-kernel --title test
ziplTest zipl.1 default/z1.2 --add-kernel /boot/new-kernel --title test --make-default

testing="GRUB fallback directive"
grubTest grub.5 fallback/g5.1 --remove-kernel=/boot/vmlinuz-2.4.7-ac3 \
    --boot-filesystem=/
grubTest grub.5 fallback/g5.2 --remove-kernel=/boot/vmlinuz-2.4.7-2.5 \
    --boot-filesystem=/
grubTest grub.5 fallback/g5.3 --remove-kernel=/boot/vmlinuz-2.4.7-2.5 \
    --boot-filesystem=/ --copy-default --add-kernel=/boot/new-kernel \
    --title="Some_Title"

testing="GRUB new kernel argument handling"
grubTest grub.1 args/g1.1 --boot-filesystem=/boot \
    --add-kernel=/boot/foo --title=some_title --args="1234" --copy-default
grubTest grub.1 args/g1.2 --boot-filesystem=/boot \
    --add-kernel=/boot/foo --title=some_title --args="1234" 

testing="GRUB remove kernel"
grubTest grub.7 remove/g7.1 --boot-filesystem=/ \
    --remove-kernel=/boot/vmlinuz-2.4.7-2.5
grubTest grub.3 remove/g3.1 --boot-filesystem=/ \
    --remove-kernel=DEFAULT
grubTest grub.9 remove/g9.1 --boot-filesystem=/boot \
    --remove-kernel=/boot/vmlinuz-2.4.7-2

testing="YABOOT remove kernel"
yabootTest yaboot.1 remove/y1.1 --boot-filesystem=/ --remove-kernel=DEFAULT
yabootTest yaboot.1 remove/y1.2 --boot-filesystem=/ --remove-kernel=/boot/vmlinuz-2.5.50-eepro
yabootTest yaboot.2 remove/y2.1 --boot-filesystem=/ --remove-kernel=/boot/vmlinux-2.5.50

testing="Z/IPL remove kernel"
ziplTest zipl.1 remove/z1.1 --remove-kernel=/boot/vmlinuz-2.4.9-38
ziplTest zipl.1 remove/z1.2 --remove-kernel=DEFAULT

testing="GRUB update kernel argument handling"
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
grubTest grub.7 updargs/g7.4 --boot-filesystem=/    \
    --update-kernel=/vmlinuz-2.4.7-2 \
    --args "ro root=LABEL=/ console=tty0 console=ttyS1,9600n81 single"
grubTest grub.11 updargs/g11.1 --boot-filesystem=/    \
    --update-kernel=/vmlinuz-2.4.7-2smp \
    --args "ro root=LABEL=/ console=tty0 console=ttyS1,9600n81 single"
grubTest grub.11 updargs/g11.2 --boot-filesystem=/    \
    --update-kernel=/vmlinuz-2.4.7-2smp \
    --args "ro root=LABEL=/ single"

testing="LILO update kernel argument handling"
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

testing="LILO add kernel"
liloTest lilo.4 add/l4.1 --add-kernel=/boot/new-kernel.img --title="title" \
    --copy-default --boot-filesystem=/boot
liloTest lilo.4 add/l4.2 --add-kernel=/boot/new-kernel.img --title="linux" \
    --copy-default --boot-filesystem=/boot --remove-kernel "TITLE=linux"
liloTest lilo.5 add/l5.1 --add-kernel=/boot/new-kernel.img --title="title" \
    --copy-default --boot-filesystem=/boot
liloTest lilo.5 add/l5.2 --add-kernel=/boot/new-kernel.img --title="linux" \
    --copy-default --boot-filesystem=/boot --remove-kernel "TITLE=linux"
liloTest lilo.6 add/l6.1 --add-kernel=/boot/new-kernel.img --title="title" \
  --initrd=/boot/new-initrd  --copy-default --boot-filesystem=/boot
liloTest lilo.6 add/l6.2 --add-kernel=/boot/new-kernel.img --title="linux" \
  --initrd=/boot/new-initrd --copy-default --boot-filesystem=/boot --remove-kernel "TITLE=linux"


testing="GRUB add kernel"
grubTest grub.1 add/g1.1 --add-kernel=/boot/new-kernel.img --title='title' \
    --initrd=/boot/new-initrd --boot-filesystem=/
grubTest grub.1 add/g1.2 --add-kernel=/boot/new-kernel.img --title='title' \
    --initrd=/boot/new-initrd --boot-filesystem=/boot
grubTest grub.1 add/g1.3 --add-kernel=/boot/new-kernel.img --title='title' \
    --initrd=/boot/new-initrd --boot-filesystem=/ --copy-default
grubTest grub.1 add/g1.4 --add-kernel=/boot/new-kernel.img --title='title' \
    --initrd=/boot/new-initrd --boot-filesystem=/boot --copy-default
grubTest grub.1 add/g1.5 --add-kernel=/boot/new-kernel.img --title='title' \
    --initrd=/boot/new-initrd --extra-initrd=/boot/extra-initrd --boot-filesystem=/
grubTest grub.1 add/g1.6 --add-kernel=/boot/new-kernel.img --title='title' \
    --initrd=/boot/new-initrd --extra-initrd=/boot/extra-initrd --boot-filesystem=/boot
grubTest grub.2 add/g2.1 --add-kernel=/boot/vmlinuz-2.4.7-2	    \
    --initrd=/boot/initrd-2.4.7-new.img --boot-filesystem=/boot --copy-default \
    --title="Red Hat Linux (2.4.7-2)"					    \
    --remove-kernel="TITLE=Red Hat Linux (2.4.7-2)" 
grubTest grub.8 add/g8.1 --add-kernel=/boot/new-kernel.img --title='title' \
    --initrd=/boot/new-initrd --boot-filesystem=/boot --copy-default
grubTest grub.8 add/g8.2 --add-kernel=/boot/new-kernel.img --title='title' \
    --initrd=/boot/new-initrd --boot-filesystem=/boot --copy-default \
    --args='console=tty0 console=ttyS1,9600n81 single'
grubTest grub.11 add/g11.1 --add-kernel=/boot/new-kernel.img --title='title' \
    --initrd=/boot/new-initrd --boot-filesystem=/boot --copy-default \
    --args='console=tty0 console=ttyS1,9600n81 single'

testing="YABOOT add kernel"
yabootTest yaboot.1 add/y1.1 --copy-default --boot-filesystem=/ --add-kernel=/boot/new-kernel  \
    --title=newtitle
yabootTest yaboot.1 add/y1.2 --add-kernel=/boot/new-kernel --boot-filesystem=/ --title=newtitle

testing="YABOOT empty label"
yabootTest yaboot.3 add/y3.1 --add-kernel=/boot/new-kernel --boot-filesystem=/ --title=newtitle

testing="Z/IPL add kernel"
ziplTest zipl.1 add/z1.1 --add-kernel=/boot/new-kernel.img --title test
ziplTest zipl.1 add/z1.2 --add-kernel=/boot/new-kernel.img --title test --copy-default

testing="LILO long titles"
liloTest lilo.1 longtitle/l1.1 --add-kernel=/boot/new-kernel.img \
    --title="linux-longtitle" --copy-default --boot-filesystem=/boot 
liloTest lilo.1 longtitle/l1.2 --add-kernel=/boot/new-kernel.img \
    --title="linux-toolongtitle" --copy-default --boot-filesystem=/boot 
liloTest lilo.7 longtitle/l7.1 --add-kernel=/boot/new-kernel.img \
    --title="linux-longtitle-fix" --copy-default --boot-filesystem=/boot 

testing="ELILO long titles"
eliloTest lilo.7 longtitle/e7.1 --add-kernel=/boot/new-kernel.img \
    --title="linux-longtitle-fix" --copy-default --boot-filesystem=/boot 

testing="GRUB add multiboot"
grubTest grub.1 multiboot/g1.1 --add-multiboot=/boot/xen.gz \
    --add-kernel=/boot/vmlinuz-2.6.10-1.1088_FC4 --boot-filesystem=/boot \
    --initrd=/boot/initrd-2.6.10-1.1088_FC4.img --title foo \
    --mbargs="dom0_mem=130000"
grubTest grub.1 multiboot/g1.2 --add-multiboot=/boot/xen.gz \
    --add-kernel=/boot/vmlinuz-2.6.10-1.1088_FC4 --boot-filesystem=/boot \
    --initrd=/boot/initrd-2.6.10-1.1088_FC4.img --title foo \
    --mbargs="dom0_mem=130000" --copy-default
grubTest grub.10 multiboot/g10.1 --add-multiboot=/boot/xen.gz \
    --add-kernel=/boot/vmlinuz-2.6.10-1.1088_FC4 --boot-filesystem=/boot \
    --initrd=/boot/initrd-2.6.10-1.1088_FC4.img --title foo \
    --mbargs="dom0_mem=130000"
grubTest grub.10 multiboot/g10.2 --add-multiboot=/boot/xen.gz \
    --add-kernel=/boot/vmlinuz-2.6.10-1.1088_FC4 --boot-filesystem=/boot \
    --initrd=/boot/initrd-2.6.10-1.1088_FC4.img --title foo \
    --mbargs="dom0_mem=130000" --copy-default
grubTest grub.10 multiboot/g10.3 --add-kernel=/boot/vmlinuz-2.6.10-1.1088_FC4 \
    --initrd=/boot/initrd-2.6.10-1.1088_FC4.img --title foo \
    --copy-default --boot-filesystem=/boot
grubTest grub.10 multiboot/g10.4 --add-kernel=/boot/vmlinuz-2.6.10-1.1088_FC4 \
    --initrd=/boot/initrd-2.6.10-1.1088_FC4.img --title foo \
    --boot-filesystem=/boot

testing="GRUB remove multiboot"
grubTest grub.10 multiboot/g10.5 --boot-filesystem=/boot \
    --remove-kernel=/boot/vmlinuz-2.6.10-1.1076_FC4
grubTest grub.10 multiboot/g10.6 --boot-filesystem=/boot \
    --remove-kernel=/boot/vmlinuz-2.6.10-1.1082_FC4
grubTest grub.10 multiboot/g10.7 --boot-filesystem=/boot \
    --remove-multiboot=/boot/xen.gz

testing="ELILO add multiboot"
eliloTest elilo.1 multiboot/e1.1 --add-multiboot=/boot/xen.gz \
    --add-kernel=/boot/vmlinuz-2.6.10-1.1088_FC4 --boot-filesystem=/boot \
    --initrd=/boot/initrd-2.6.10-1.1088_FC4.img --title foo \
    --mbargs="dom0_mem=130000"
eliloTest elilo.1 multiboot/e1.2 --add-multiboot=/boot/xen.gz \
    --add-kernel=/boot/vmlinuz-2.6.10-1.1088_FC4 --boot-filesystem=/boot \
    --initrd=/boot/initrd-2.6.10-1.1088_FC4.img --title foo \
    --mbargs="dom0_mem=130000" --copy-default

testing="ELILO remove multiboot"
eliloTest elilo.2 multiboot/e2.1 --boot-filesystem=/boot \
    --remove-kernel=/boot/vmlinuz-2.6.10-1.1076_FC4
eliloTest elilo.2 multiboot/e2.2 --boot-filesystem=/boot \
    --remove-kernel=/boot/vmlinuz-2.6.10-1.1082_FC4
eliloTest elilo.2 multiboot/e2.3 --boot-filesystem=/boot \
    --remove-multiboot=/boot/xen.gz

printf "\n%d (%d%%) tests passed, %d (%d%%) tests failed\n" \
    $pass $(((100*pass)/(pass+fail))) \
    $fail $(((100*fail)/(pass+fail)))

exit $(( !!fail ))
