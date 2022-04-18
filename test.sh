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

    local ENV_FILE=""
    if [ "$mode" == "--grub2" ]; then
        ENV_FILE="test/grub2-support_files/env_temp"
        if [ "$1" == "--env" ]; then
            cp "test/grub2-support_files/$2" "$ENV_FILE"
            shift 2
        else
            cp "test/grub2-support_files/grubenv.0" "$ENV_FILE"
        fi
        ENV_FILE="--env=$ENV_FILE"
    fi


    echo "$testing ... $mode $cfg $correct"
    runme=( ./grubby "$mode" --bad-image-okay $ENV_FILE -c "$cfg" -o - "$@" )
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

# Test feature that display some information, checking that output instead of
# the generated configuration file
oneDisplayTest() {
    typeset mode=$1 cfg=test/$2 correct=test/results/$3
    shift 3

    local ENV_FILE=""
    if [ "$mode" == "--grub2" ]; then
        ENV_FILE="test/grub2-support_files/env_temp"
        if [ "$1" == "--env" ]; then
            cp "test/grub2-support_files/$2" "$ENV_FILE"
            shift 2
        else
            cp "test/grub2-support_files/grubenv.0" "$ENV_FILE"
        fi
        ENV_FILE="--env=$ENV_FILE"
    fi

    local BIO="--bad-image-okay"
    if [ "$1" == "--bad-image-bad" ]; then
        BIO=""
        shift
    fi

    echo "$testing ... $mode $cfg $correct"
    runme=( ./grubby "$mode" $BIO $ENV_FILE -c "$cfg" "$@" )
    if "${runme[@]}" 2>&1 | cmp "$correct" > /dev/null; then
	(( pass++ ))
	if $opt_verbose; then
	    echo -------------------------------------------------------------
	    echo -n "PASS: "
	    printf "%q " "${runme[@]}"; echo
	    "${runme[@]}" 2>&1 | diff -U30 "$cfg" -
	    echo
	fi
    else
	(( fail++ ))
	echo -------------------------------------------------------------
	echo -n "FAIL: "
	printf "%q " "${runme[@]}"; echo
	"${runme[@]}" 2>&1 | diff -U30 "$correct" -
	echo
    fi
}

commandTest() {
    description=$1
    cmd0=$2
    text1=$3
    shift 3
    echo "$description"
    output0=$(mktemp)

    $cmd0 > $output0

    if echo $text1 | cmp $output0 - >/dev/null; then
	(( pass++))
	if $opt_verbose; then
	    echo -------------------------------------------------------------
	    echo -n "PASS: "
	    printf "%q " "\"$cmd0\""; echo
	    echo $text1 | diff -U30 $output0 -
	    echo
	fi
    else
	(( fail++ ))
	echo -------------------------------------------------------------
	echo -n "FAIL: "
	printf "%q " "\"$cmd0\""; echo
	echo $text1 | diff -U30 $output0 -
	echo
    fi
}

# generate convenience functions
for b in $(./grubby --help | \
	sed -n 's/^.*--\([^ ]*\) *configure \1 bootloader.*/\1/p'); do
    eval "${b}Test() { [[ \"$b\" == \$opt_bootloader ]] && oneTest --$b \"\$@\"; }"
    eval "${b}DisplayTest() { [[ \"$b\" == \$opt_bootloader ]] && oneDisplayTest --$b \"\$@\"; }"
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
if [ -n "${RANDOM}" ]; then
    export MALLOC_PERTURB_=$(($RANDOM % 255 + 1))
else
    export MALLOC_PERTURB_=1
fi

testing="Parse/write comparison"
for n in test/*.[0-9]*; do
    [ -d $n ] && continue
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

testing="GRUB default index directive"
grubTest grub.13 setdefaultindex/g.13.0 --set-default-index=0
grubTest grub.13 setdefaultindex/g.13.1 --set-default-index=1
grubTest grub.13 setdefaultindex/g.13.9 --set-default-index=9

testing="GRUB add initrd"
grubTest grub.14 add/g1.7 --boot-filesystem=/ --update-kernel=/vmlinuz-4.0.0-0.rc4.git1.4.fc23.x86_64 --initrd /initramfs-4.0.0-0.rc4.git1.4.fc23.x86_64.img '--args= LANG=en_US.UTF-8' '--title=Fedora (4.0.0-0.rc4.git1.4.fc23.x86_64) 23 (Rawhide)'

testing="GRUB display default index"
grubDisplayTest grub.1 defaultindex/0 --default-index
grubDisplayTest grub.2 defaultindex/0 --default-index
grubDisplayTest grub.3 defaultindex/0 --default-index
grubDisplayTest grub.4 defaultindex/0 --default-index
grubDisplayTest grub.5 defaultindex/0 --default-index
grubDisplayTest grub.6 defaultindex/2 --default-index
grubDisplayTest grub.7 defaultindex/2 --default-index
grubDisplayTest grub.8 defaultindex/0 --default-index
grubDisplayTest grub.9 defaultindex/0 --default-index
grubDisplayTest grub.10 defaultindex/0 --default-index
grubDisplayTest grub.10 defaultindex/0 --default-index

testing="GRUB display default title"
grubDisplayTest grub.1 defaulttitle/g.1 --default-title
grubDisplayTest grub.2 defaulttitle/g.2 --default-title
grubDisplayTest grub.3 defaulttitle/g.3 --default-title
grubDisplayTest grub.4 defaulttitle/g.4 --default-title
grubDisplayTest grub.5 defaulttitle/g.5 --default-title
grubDisplayTest grub.6 defaulttitle/g.6 --default-title
grubDisplayTest grub.7 defaulttitle/g.7 --default-title
grubDisplayTest grub.8 defaulttitle/g.8 --default-title
grubDisplayTest grub.9 defaulttitle/g.9 --default-title
grubDisplayTest grub.10 defaulttitle/g.10 --default-title
grubDisplayTest grub.11 defaulttitle/g.11 --default-title

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

testing="Extlinux default directive"
extlinuxTest extlinux.1 default/extlinux1.1 --boot-filesystem=/boot --add-kernel /boot/new-kernel --title Some_Title
extlinuxTest extlinux.1 default/extlinux1.2 --boot-filesystem=/boot --add-kernel /boot/new-kernel --title Some_Title --make-default
extlinuxTest extlinux.3 default/extlinux3.1 --boot-filesystem=/boot --set-default=/boot/vmlinuz-3.12.0-2.fc21.i686
extlinuxTest extlinux.3 default/extlinux3.2 --boot-filesystem=/boot --set-default=/boot/vmlinuz-3.12.0-2.fc21.i686+PAE

testing="GRUB new kernel argument handling"
grubTest grub.1 args/g1.1 --boot-filesystem=/boot \
    --add-kernel=/boot/foo --title=some_title --args="1234" --copy-default
grubTest grub.1 args/g1.2 --boot-filesystem=/boot \
    --add-kernel=/boot/foo --title=some_title --args="1234" 

testing="Extlinux new kernel argument handling"
extlinuxTest extlinux.1 args/extlinux1.1 --boot-filesystem=/boot \
    --add-kernel=/boot/foo --title=some_title --args="1234" --copy-default
extlinuxTest extlinux.1 args/extlinux1.2 --boot-filesystem=/boot \
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

testing="Extlinux remove kernel"
extlinuxTest extlinux.4 remove/extlinux4.1 --boot-filesystem=/ \
    --remove-kernel=/boot/vmlinuz-3.11.7-301.fc20.i686
extlinuxTest extlinux.3 remove/extlinux3.1 --boot-filesystem=/ \
    --remove-kernel=DEFAULT

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
grubTest grub.7 updargs/g7.3 --boot-filesystem=/    \
    --update-kernel=DEFAULT --args "hde=ide-scsi"
grubTest grub.7 updargs/g7.4 --boot-filesystem=/    \
    --update-kernel=/vmlinuz-2.4.7-2 \
    --args "ro root=LABEL=/ console=tty0 console=ttyS1,9600n81 single"
grubTest grub.7 updargs/g7.5 --boot-filesystem=/    \
    --update-kernel=ALL --args "root=/dev/hda2"
grubTest grub.11 updargs/g11.1 --boot-filesystem=/    \
    --update-kernel=/vmlinuz-2.4.7-2smp \
    --args "ro root=LABEL=/ console=tty0 console=ttyS1,9600n81 single"
grubTest grub.11 updargs/g11.2 --boot-filesystem=/    \
    --update-kernel=/vmlinuz-2.4.7-2smp \
    --args "ro root=LABEL=/ single"

testing="GRUB lba and root information on SuSE systems"
GRUBBY_SUSE_RELEASE=test/grub.12-support_files/etc/SuSE-release \
    GRUBBY_SUSE_GRUB_CONF=test/grub.12-support_files/etc/grub.conf \
    GRUBBY_GRUB_DEVICE_MAP=test/grub.12-support_files/boot/grub/device.map \
    grubTest grub.12 info/g12.1 --info=0

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

testing="Extlinux update kernel argument handling"
extlinuxTest extlinux.1 updargs/extlinux1.1 --update-kernel=DEFAULT --args="root=/dev/hda1"
extlinuxTest extlinux.1 updargs/extlinux1.2 --update-kernel=DEFAULT \
    --args="root=/dev/hda1 hda=ide-scsi root=/dev/hda2"
extlinuxTest extlinux.3 updargs/extlinux3.1 --update-kernel=DEFAULT --args "hdd=notide-scsi"
extlinuxTest extlinux.3 updargs/extlinux3.2 --update-kernel=DEFAULT \
    --args "hdd=notide-scsi root=/dev/hdd1"
extlinuxTest extlinux.3 updargs/extlinux3.2 --update-kernel=DEFAULT \
    --args "root=/dev/hdd1 hdd=notide-scsi"
extlinuxTest extlinux.3 updargs/extlinux3.4 --update-kernel=ALL --remove-args="hdd"
extlinuxTest extlinux.3 updargs/extlinux3.4 --update-kernel=ALL --remove-args="hdd=ide-scsi"
extlinuxTest extlinux.3 updargs/extlinux3.4 --update-kernel=ALL --remove-args="hdd=foobar"
extlinuxTest extlinux.3 updargs/extlinux3.7 --update-kernel=ALL \
    --remove-args="hdd root ro"
extlinuxTest extlinux.4 updargs/extlinux4.2 --boot-filesystem=/    \
    --update-kernel=ALL --args "hde=ide-scsi"
extlinuxTest extlinux.4 updargs/extlinux4.3 --boot-filesystem=/    \
    --update-kernel=DEFAULT --args "hde=ide-scsi"
extlinuxTest extlinux.4 updargs/extlinux4.4 --boot-filesystem=/    \
    --update-kernel=/vmlinuz-3.12.0-2.fc21.i686 \
    --args "ro root=LABEL=/ console=tty0 console=ttyS1,9600n81 single"
extlinuxTest extlinux.4 updargs/extlinux4.5 --boot-filesystem=/    \
    --update-kernel=ALL --args "root=/dev/hda2"

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

testgrub2=n
ARCH=$(uname -m | sed s,i[3456789]86,ia32,)
case $ARCH in
    aarch64|ppc|ppc64|ia32|x86_64) testgrub2=y ;;
esac

if [ "$testgrub2" == "y" ]; then
    testing="GRUB2 add kernel"
    grub2Test grub2.1 add/g2-1.1 --add-kernel=/boot/new-kernel.img \
        --title='title' --initrd=/boot/new-initrd --boot-filesystem=/boot/ \
        --copy-default
    case $ARCH in
        aarch64)
            grub2Test grub2.1 add/g2-1.1 --add-kernel=/boot/new-kernel.img \
                --title='title' \
                --initrd=/boot/new-initrd --boot-filesystem=/boot/ \
                --copy-default --efi
            ;;
        *)
            grub2Test grub2.1 add/g2-1.6 --add-kernel=/boot/new-kernel.img \
                --title='title' \
                --initrd=/boot/new-initrd --boot-filesystem=/boot/ \
                --copy-default --efi
            ;;
    esac
    grub2Test grub2.6 add/g2-1.7 --add-kernel=/boot/new-kernel.img \
        --title='title' --initrd=/boot/new-initrd --boot-filesystem=/boot/ \
        --copy-default --efi
    grub2Test grub2.1 add/g2-1.2 --add-kernel=/boot/new-kernel.img \
        --title='title' --initrd=/boot/new-initrd --boot-filesystem=/boot/ \
        --copy-default --make-default
    grub2Test grub2.1 add/g2-1.3 --add-kernel=/boot/new-kernel.img \
        --title='title' --boot-filesystem=/boot/ --copy-default --make-default
    grub2Test grub2.1 remove/g2-1.4 \
        --remove-kernel=/boot/vmlinuz-2.6.38.2-9.fc15.x86_64 \
        --boot-filesystem=/boot/
    grub2Test grub2.5 add/g2-1.5 --add-kernel=/boot/new-kernel.img \
        --title='title' --initrd=/boot/new-initrd --boot-filesystem=/boot/ \
        --copy-default
    grub2Test grub2.12 add/g2-1.12 \
        --add-kernel=/boot/vmlinuz-2.6.38.8-32.fc15.x86_64 \
        --title='Linux, with Fedora 2.6.38.8-32.fc15.x86_64' \
        --devtree='/boot/dtb-2.6.38.8-32.fc15.x86_64/foobarbaz.dtb' \
        --initrd=/boot/initramfs-2.6.38.8-32.fc15.x86_64.img \
        --boot-filesystem=/boot/ --copy-default --efi
    grub2Test grub2.13 add/g2-1.13 \
        --add-kernel=/boot/vmlinuz-2.6.38.8-32.fc15.x86_64 \
        --title='Linux, with Fedora 2.6.38.8-32.fc15.x86_64' \
        --devtree='/boot/dtb-2.6.38.8-32.fc15.x86_64/foobarbaz.dtb' \
        --initrd=/boot/initramfs-2.6.38.8-32.fc15.x86_64.img \
        --boot-filesystem=/boot/ --copy-default --efi
    grub2Test grub2.15 add/g2-1.15 \
        --add-kernel=/boot/vmlinuz-0-rescue-5a94251776a14678911d4ae0949500f5 \
        --initrd /boot/initramfs-0-rescue-5a94251776a14678911d4ae0949500f5.img \
        --copy-default --title "Fedora 21 Rescue" --args=root=/fooooo \
        --remove-kernel=wtf --boot-filesystem=/boot/ --efi

    testing="GRUB2 add initrd"
    grub2Test grub2.2 add/g2-1.4 --update-kernel=/boot/new-kernel.img \
        --initrd=/boot/new-initrd --boot-filesystem=/boot/

    testing="GRUB2 display default index"
    grub2DisplayTest grub2.1 defaultindex/0 --default-index
    grub2DisplayTest grub2.2 defaultindex/0 --default-index

    testing="GRUB2 display default title"
    grub2DisplayTest grub2.1 defaulttitle/g2.1 --default-title
    grub2DisplayTest grub2.2 defaulttitle/g2.2 --default-title

    testing="GRUB2 display debug failure"
    grub2DisplayTest grub2.1 debug/g2.1 --bad-image-bad \
        --boot-filesystem=/boot --default-kernel --debug
    testing="GRUB2 display debug success"
    grub2DisplayTest grub2.1 debug/g2.1.2 --boot-filesystem=/boot \
        --default-kernel --debug

    testing="GRUB2 remove kernel via index"
    grub2Test grub2.3 remove/g2-1.1 --remove-kernel=1

    testing="GRUB2 remove kernel via title"
    grub2Test grub2.3 remove/g2-1.1 --remove-kernel="TITLE=title2"

    testing="GRUB2 (submenu) remove kernel via index"
    grub2Test grub2.4 remove/g2-1.2 --remove-kernel=2

    testing="GRUB2 (submenu) remove kernel via title"
    grub2Test grub2.4 remove/g2-1.2 --remove-kernel="TITLE=title2"

    testing="GRUB2 default index directive"
    grub2Test grub2.1 setdefaultindex/g2.1.0 --set-default-index=0
    grub2Test grub2.1 setdefaultindex/g2.1.1 --set-default-index=1
    grub2Test grub2.1 setdefaultindex/g2.1.9 --set-default-index=9

    testing="GRUB2 add kernel with default=saved_entry"
    grub2Test grub2.7 add/g2-1.8 --env grubenv.1 \
        --add-kernel=/boot/new-kernel.img \
        --title='title' --initrd=/boot/new-initrd --boot-filesystem=/boot/ \
        --copy-default
    commandTest "saved_default output" \
        "grub2-editenv test/grub2-support_files/env_temp list" \
        "saved_entry=Linux, with Fedora 2.6.38.8-32.fc15.x86_64"

    testing="GRUB2 add kernel with default=saved_entry and a terrible title"
    grub2Test grub2.7 add/g2-1.9 --env grubenv.1 \
        --add-kernel=/boot/new-kernel.img \
        --title='Fedora (3.10.3-300.fc19.x86_64) 19 (Schrödinger’s Cat)' \
        --initrd=/boot/new-initrd --boot-filesystem=/boot/ \
        --copy-default

    testing="GRUB2 set default with default=saved_entry and a terrible name"
    grub2Test grub2.9 add/g2-1.9 --env grubenv.1 --set-default-index=0
    commandTest "saved_default output" \
        "grub2-editenv test/grub2-support_files/env_temp list" \
        'saved_entry=Fedora (3.10.3-300.fc19.x86_64) 19 (Schrödinger’s Cat)'

    testing="GRUB2 set default with default=saved_entry"
    grub2Test grub2.8 add/g2-1.8 --env grubenv.1 --set-default-index=0
    commandTest "saved_default output" \
        "grub2-editenv test/grub2-support_files/env_temp list" \
        "saved_entry=title"

    testing="GRUB2 --default-index with default=saved_entry"
    grub2DisplayTest grub2.8 defaultindex/1 --env grubenv.1 --default-index

    testing="GRUB2 --default-index with default=saved_entry"
    grub2DisplayTest grub2.8 defaultindex/0 --env grubenv.2 --default-index

    testing="GRUB2 --default-title with default=saved_entry"
    grub2DisplayTest grub2.8 defaulttitle/g2.1 --env grubenv.1 --default-title

    testing="GRUB2 --default-index with default=saved_entry and empty grubenv"
    grub2DisplayTest grub2.8 defaultindex/0 --env grubenv.0 --default-index

    testlinux16=n
    case $ARCH in
        ia32|x86_64) testlinux16=y ;;
    esac

    if [ "$testlinux16" == "y" ]; then
        testing="GRUB2 add kernel with linux16"
        grub2Test grub2.10 add/g2-1.10 --add-kernel=/boot/new-kernel.img \
            --title='title' --initrd=/boot/new-initrd --boot-filesystem=/boot/ \
            --copy-default

        testing="GRUB2 add initrd with linux16"
        grub2Test grub2.11 add/g2-1.11 --update-kernel=/boot/new-kernel.img \
            --initrd=/boot/new-initrd --boot-filesystem=/boot/

        testing="GRUB2 add rescue with linux16"
        grub2Test grub2.14 add/g2-1.14 \
            --add-kernel=/boot/vmlinuz-0-rescue-5a94251776a14678911d4ae0949500f5 \
            --initrd /boot/initramfs-0-rescue-5a94251776a14678911d4ae0949500f5.img \
            --copy-default --title "Fedora 21 Rescue" --args=root=/fooooo \
            --remove-kernel=wtf --boot-filesystem=/boot/

        testing="GRUB2 add kernel with boot on btrfs subvol"
        grub2Test grub2.20 add/g2-1.20 --add-kernel=/boot/new-kernel.img \
            --title='title' \
            --boot-filesystem=/boot/ \
            --copy-default \
            --mounts='test/grub2-support_files/g2.20-mounts'

        testing="GRUB2 add initrd with boot on btrfs subvol"
        grub2Test grub2.21 add/g2-1.21 --update-kernel=/boot/new-kernel.img \
            --initrd=/boot/new-initrd --boot-filesystem=/boot/ \
            --mounts='test/grub2-support_files/g2.21-mounts'

        testing="GRUB2 add kernel with rootfs on btrfs subvol and boot directory"
        grub2Test grub2.22 add/g2-1.22 --add-kernel=/boot/new-kernel.img \
            --title='title' \
            --boot-filesystem= \
            --copy-default \
            --mounts='test/grub2-support_files/g2.22-mounts'

        testing="GRUB2 add initrd with rootfs on btrfs subvol and boot directory"
        grub2Test grub2.23 add/g2-1.23 --update-kernel=/boot/new-kernel.img \
            --initrd=/boot/new-initrd --boot-filesystem= \
            --mounts='test/grub2-support_files/g2.23-mounts'

        testing="GRUB2 add kernel and initrd with boot on btrfs subvol"
        grub2Test grub2.24 add/g2-1.24 --add-kernel=/boot/new-kernel.img \
            --title='title' \
            --initrd=/boot/new-initrd \
            --boot-filesystem=/boot/ \
            --copy-default \
            --mounts='test/grub2-support_files/g2.24-mounts'

        testing="GRUB2 add kernel and initrd with rootfs on btrfs subvol and boot directory"
        grub2Test grub2.25 add/g2-1.25 --add-kernel=/boot/new-kernel.img \
            --title='title' \
            --initrd=/boot/new-initrd \
            --boot-filesystem= \
            --copy-default \
            --mounts='test/grub2-support_files/g2.25-mounts'
    fi
fi

testing="YABOOT add kernel"
yabootTest yaboot.1 add/y1.1 --copy-default --boot-filesystem=/ --add-kernel=/boot/new-kernel  \
    --title=newtitle
yabootTest yaboot.1 add/y1.2 --add-kernel=/boot/new-kernel --boot-filesystem=/ --title=newtitle

testing="YABOOT empty label"
yabootTest yaboot.3 add/y3.1 --add-kernel=/boot/new-kernel --boot-filesystem=/ --title=newtitle

testing="Z/IPL add kernel"
ziplTest zipl.1 add/z1.1 --add-kernel=/boot/new-kernel.img --title test
ziplTest zipl.1 add/z1.2 --add-kernel=/boot/new-kernel.img --title test --copy-default

testing="Extlinux add kernel"
extlinuxTest extlinux.1 add/extlinux1.1 --add-kernel=/boot/new-kernel.img \
    --title='title' --initrd=/boot/new-initrd --boot-filesystem=/
extlinuxTest extlinux.1 add/extlinux1.2 --add-kernel=/boot/new-kernel.img \
    --title='title' --initrd=/boot/new-initrd --boot-filesystem=/boot
extlinuxTest extlinux.1 add/extlinux1.3 --add-kernel=/boot/new-kernel.img \
    --title='title' --initrd=/boot/new-initrd --boot-filesystem=/ \
    --copy-default
extlinuxTest extlinux.1 add/extlinux1.4 --add-kernel=/boot/new-kernel.img \
    --title='title' --initrd=/boot/new-initrd --boot-filesystem=/boot \
    --copy-default
extlinuxTest extlinux.2 add/extlinux2.1 \
    --add-kernel=/boot/vmlinuz-3.12.0-2.fc21.i686 \
    --initrd=/boot/initrd-3.12.0-2.fc21.i686-new.img \
    --boot-filesystem=/boot --copy-default \
    --title="Fedora (3.12.0-2.fc21.i686) 20 (Heisenbug)" \
    --remove-kernel="TITLE=Fedora (3.12.0-2.fc21.i686) 20 (Heisenbug)"
extlinuxTest extlinux.5 add/extlinux5.1 \
    --add-kernel=/boot/vmlinuz-3.15.0-0.rc1.git4.1.fc21.armv7hl \
    --devtree='/boot/dtb-3.15.0-0.rc1.git4.1.fc21.armv7hl/imx6q-cubox-i.dtb' \
    --initrd=/boot/initramfs-3.15.0-0.rc1.git4.1.fc21.armv7hl.img \
    --boot-filesystem=/boot --copy-default \
    --title="Fedora (3.15.0-0.rc1.git4.1.fc21.armv7hl) 21 (Rawhide)" \
    --remove-kernel="TITLE=Fedora (3.12.0-0.fc21.armv7hl) 21 (Rawhide)"
extlinuxTest extlinux.6 add/extlinux6.1 \
    --add-kernel=/boot/vmlinuz-3.15.0-0.rc1.git4.1.fc21.armv7hl \
    --devtreedir='/boot/dtb-3.15.0-0.rc1.git4.1.fc21.armv7hl/' \
    --initrd=/boot/initramfs-3.15.0-0.rc1.git4.1.fc21.armv7hl.img \
    --boot-filesystem=/boot --copy-default \
    --title="Fedora (3.15.0-0.rc1.git4.1.fc21.armv7hl) 21 (Rawhide)" \
    --remove-kernel="TITLE=Fedora (3.12.0-0.fc21.armv7hl) 21 (Rawhide)"

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
