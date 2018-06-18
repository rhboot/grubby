#!/bin/bash
#
# test.sh -- grubby wrapper regression tests
#
# Copyright 2007-2018 Red Hat, Inc.  All rights reserved.
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

BLSDIR=$(mktemp -d)

#----------------------------------------------------------------------
# Functions
#----------------------------------------------------------------------

empty_blsdir() {
    rm -rf $BLSDIR/*
}

oneTest() {
    typeset mode=$1 correct=test/bls/results/$2
    shift 2

    local ENV_FILE=""
    if [ "$mode" == "--grub2" ]; then
	ENV_FILE="test/bls/grub2-support_files/env_temp"
	if [ ! -f $ENV_FILE ]; then
            cp "test/bls/grub2-support_files/grubenv" "$ENV_FILE"
	fi
        ENV_FILE="--env=$ENV_FILE"
    fi

    local CFG_FILE=""
    if [ "$mode" == "--zipl" ]; then
	CFG_FILE=$BLSDIR/cfg_temp
	if [ ! -f $CFG_FILE ]; then
            cp "test/bls/zipl-support_files/zipl.conf" "$CFG_FILE"
	fi
        CFG_FILE="--config-file=$CFG_FILE"
    fi

    echo "$testing ... $mode $correct"
    runme=( ./grubby-bls "$mode" --bad-image-okay $ENV_FILE $CFG_FILE -b $BLSDIR "$@" )
    runtest=( ./grubby-bls "$mode" --info=ALL $ENV_FILE $CFG_FILE -b $BLSDIR)
    if "${runme[@]}" && "${runtest[@]}" | cmp "$correct" > /dev/null; then
	(( pass++ ))
	if $opt_verbose; then
	    echo -------------------------------------------------------------
	    echo -n "PASS: "
	    printf "%q " "${runme[@]}"; echo
	    "${runtest[@]}" | diff -U30 "$correct" -
	    echo
	fi
    else
	(( fail++ ))
	echo -------------------------------------------------------------
	echo -n "FAIL: "
	printf "%q " "${runme[@]}"; echo
	"${runtest[@]}" | diff -U30 "$correct" -
	echo
    fi
}

# Test feature that display some information, checking that output instead of
# the generated configuration file
oneDisplayTest() {
    typeset mode=$1 correct=test/bls/results/$2
    shift 2

    local ENV_FILE=""
    if [ "$mode" == "--grub2" ]; then
	ENV_FILE="test/bls/grub2-support_files/env_temp"
	if [ ! -f $ENV_FILE ]; then
            cp "test/bls/grub2-support_files/grubenv" "$ENV_FILE"
	fi
        ENV_FILE="--env=$ENV_FILE"
    fi

    local CFG_FILE=""
    if [ "$mode" == "--zipl" ]; then
	CFG_FILE=$BLSDIR/cfg_temp
	if [ ! -f $CFG_FILE ]; then
            cp "test/bls/zipl-support_files/zipl.conf" "$CFG_FILE"
	fi
        CFG_FILE="--config-file=$CFG_FILE"
    fi

    local BIO="--bad-image-okay"
    if [ "$1" == "--bad-image-bad" ]; then
        BIO=""
        shift
    fi

    echo "$testing ... $mode $correct"
    runme=( ./grubby-bls "$mode" $BIO $ENV_FILE $CFG_FILE -b $BLSDIR "$@" )
    if "${runme[@]}" 2>&1 | cmp "$correct" > /dev/null; then
	(( pass++ ))
	if $opt_verbose; then
	    echo -------------------------------------------------------------
	    echo -n "PASS: "
	    printf "%q " "${runme[@]}"; echo
	    "${runme[@]}" 2>&1 | diff -U30 "$correct" -
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
for b in $(./grubby-bls --help | \
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

empty_blsdir

testing="Z/IPL default directive"
ziplTest add/z1.1 --add-kernel /boot/new-kernel0 --title "title 0"
ziplTest add/z1.2 --add-kernel /boot/new-kernel1 --title "title 1" --make-default

testing="Z/IPL display default index"
ziplDisplayTest default/index0 --default-index

testing="Z/IPL default index directive"
ziplTest default/z2 --set-default-index=1

testing="Z/IPL display default index"
ziplDisplayTest default/index1 --default-index

testing="Z/IPL display default title"
ziplDisplayTest default/title0 --default-title

testing="Z/IPL display default kernel"
ziplDisplayTest default/kernel0 --default-kernel

testing="Z/IPL display entry information"
ziplDisplayTest display/info0 --info=1

testing="Z/IPL remove kernel"
ziplTest remove/z3.1 --remove-kernel=/boot/new-kernel1
ziplTest remove/z3.2 --remove-kernel=DEFAULT

testing="Z/IPL add kernel"
ziplTest add/z4.1 --add-kernel=/boot/new-kernel0 --title test --args="foo=bar"
ziplTest add/z4.2 --add-kernel=/boot/new-kernel1 --title test --copy-default --args="x=y"

empty_blsdir

testing="GRUB2 add kernel with title"
grub2Test add/g1 --add-kernel=/boot/new-kernel0 --title='title 0' \
	  --initrd=/boot/new-initrd0.img

testing="GRUB2 add kernel with initrd"
grub2Test add/g2 --add-kernel=/boot/new-kernel1 --initrd=/boot/new-initrd1.img \
	  --title="title 1" --make-default

testing="GRUB2 add kernel"
grub2Test add/g3 --add-kernel=/boot/new-kernel2 --title "title 2"

testing="GRUB2 remove kernel"
grub2Test remove/g4 --remove-kernel=/boot/new-kernel2

testing="GRUB2 add kernel with copy default"
grub2Test add/g5 --add-kernel=/boot/new-kernel3.img --title='title 3' \
	  --initrd=/boot/new-initrd3.img --copy-default

testing="GRUB2 add kernel with args and ignore remove kernel"
grub2Test add/g6 --add-kernel=/boot/vmlinuz-0-rescue-5a94251776a14678911d4ae0949500f5 \
          --initrd /boot/initramfs-0-rescue-5a94251776a14678911d4ae0949500f5.img \
          --copy-default --title "Fedora 21 Rescue" --args=root=/fooooo \
          --remove-kernel=wtf

testing="GRUB2 add initrd"
grub2Test add/g7 --update-kernel=/boot/new-kernel0 --initrd=/boot/new-initrd

testing="GRUB2 display default index"
grub2DisplayTest default/index1 --default-index

testing="GRUB2 display default title"
grub2DisplayTest default/title1 --default-title

testing="GRUB2 remove kernel via index"
grub2Test remove/g8 --remove-kernel=1

testing="GRUB2 remove kernel via title"
grub2Test remove/g9 --remove-kernel="TITLE=title 3"

testing="GRUB2 default index directive"
grub2Test default/g10.1 --set-default-index=0
grub2DisplayTest default/index0 --default-index
grub2Test default/g10.2 --set-default-index=1
grub2DisplayTest default/index1 --default-index
grub2Test default/g10.3 --set-default-index=9
grub2DisplayTest default/index1 --default-index

testing="GRUB2 add kernel with default=saved_entry"
grub2Test add/g11 --add-kernel=/boot/new-kernel.img \
          --title='title' --initrd=/boot/new-initrd \
          --copy-default
commandTest "saved_default output" \
	    "grub2-editenv test/bls/grub2-support_files/env_temp list" \
	    "saved_entry=Fedora 21 Rescue"

testing="GRUB2 add kernel with default=saved_entry and a terrible title"
grub2Test add/g12 --add-kernel=/boot/new-kernel.img \
          --title='Fedora (3.10.3-300.fc19.x86_64) 19 (Schrödinger’s Cat)' \
          --initrd=/boot/new-initrd --copy-default

testing="GRUB2 set default with default=saved_entry and a terrible name"
grub2Test add/g13 --set-default-index=1
commandTest "saved_default output" \
            "grub2-editenv test/bls/grub2-support_files/env_temp list" \
            'saved_entry=Fedora (3.10.3-300.fc19.x86_64) 19 (Schrödinger’s Cat)'

testing="GRUB2 set default with default=saved_entry"
grub2Test add/g14 --set-default-index=0
commandTest "saved_default output" \
            "grub2-editenv test/bls/grub2-support_files/env_temp list" \
            "saved_entry=title 0"

testing="GRUB2 --default-index with default=saved_entry"
grub2DisplayTest default/index0 --default-index

testing="GRUB2 --default-title with default=saved_entry"
grub2DisplayTest default/title0 --default-title

testing="GRUB2 --default-index with default=saved_entry and empty grubenv"
grub2DisplayTest default/index0 --env /dev/null --default-index

printf "\n%d (%d%%) tests passed, %d (%d%%) tests failed\n" \
    $pass $(((100*pass)/(pass+fail))) \
    $fail $(((100*fail)/(pass+fail)))

rm -rf $BLSDIR

exit $(( !!fail ))
