#!/bin/sh
red='\033[1;31m'
green='\033[1;32m' 
NC='\033[0m' # No Color
#make clean 1>&2 >/dev/null
#make 1>&2 >/dev/null


host=`uname -s`


if [ "$USE_VALGRIND" = "1" ]; then
    RUN="valgrind --tool=memcheck --leak-check=yes  --track-origins=yes"
    OUT= ""
else 
    OUT=">/dev/null 2>&1"
fi

print_status () {
    status=$1
    if [ $status = 0 ]; then
	printf "${green}[OK]${NC}\n"; 
    else 
	printf "${red}[FAILED]${NC}\n"; 
    fi
}

create_test_file () {
    if [ ! -f "${1}.unc" ]; then
	dd if=/dev/urandom of=./$1.unc bs=1024x1024 count=200 2>/dev/null
    fi
    if [ ! -f "$1" ]; then
	dd if=/dev/urandom of=./$1.tmp bs=1024x1024 count=10 2>/dev/null
	for i in `seq 1 20`; do
	    cat ./$1.tmp >> ./$1
	    dd if=/dev/urandom of=./tmp.tmp bs=1024x100 count=$i 2>/dev/null
	    cat ./tmp.tmp >> ./$1
	done
    fi
    if [ ! -f "${1}2" ]; then
	dd if=/dev/urandom of=./$1.tmp2 bs=1024x1024 count=10  2>/dev/null
	cat ./$1.tmp >> ./${1}2
	cat ./$1.tmp2 >> ./${1}2
	cat ./$1.tmp >> ./${1}2
	cat ./$1.tmp2 >> ./${1}2
    fi
    rm -rf ./tmp.tmp ./$1.tmp ./$1.tmp2
    
}

comp_ratio () {
    if [ "$host" = "Darwin" ]; then
    eval $(stat -s ${1}.comp)
    comps=$st_size
    eval $(stat -s ${1})
    orgs=$st_size
    else
    comps=`stat -c%s ${1}.comp`
    orgs=`stat -c%s ${1}`
    fi
    save=$(($orgs - $comps))
    saver=$((($save * 100) / $orgs))
    printf "%2d%%\t" $saver
}

TEST_FILE=randomfile
create_test_file $TEST_FILE
printf "Test file created       \n"
#----------------------------------------------------------------
printf "Comp/Decomp uncomp Binary       "
$RUN ./compress 16 15 256 8192 $TEST_FILE.unc $OUT
$RUN ./decompress $TEST_FILE.unc.comp $OUT
comp_ratio $TEST_FILE.unc
diff $TEST_FILE.unc $TEST_FILE.unc.comp.decomp >/dev/null 2>&1
print_status $?

#----------------------------------------------------------------

rm -rf  $TEST_FILE.comp.decomp $TEST_FILE.comp $OUT
printf "Compress/Decompress Binary       "
$RUN ./compress 16 15 256 8192 $TEST_FILE $OUT
$RUN ./decompress $TEST_FILE.comp $OUT
comp_ratio $TEST_FILE
diff $TEST_FILE $TEST_FILE.comp.decomp >/dev/null 2>&1
print_status $?

#----------------------------------------------------------------
rm -rf  $TEST_FILE.comp.decomp
printf "Decompress fragmented blk        "
$RUN ./decomp_fragblock $TEST_FILE.comp $OUT
comp_ratio $TEST_FILE
diff $TEST_FILE $TEST_FILE.comp.decomp >/dev/null 2>&1
print_status $?

#----------------------------------------------------------------
printf "Load existing cache              "
$RUN ./comp_loadcache 16 15 256 8192 ${TEST_FILE}2 $OUT
$RUN ./decomp_loadcache ${TEST_FILE}2.comp $OUT
comp_ratio ${TEST_FILE}2
diff ${TEST_FILE}2 ${TEST_FILE}2.comp.decomp >/dev/null 2>&1
print_status $?
rm -rf  $TEST_FILE.comp.decomp $TEST_FILE.comp >/dev/null 2>&1
#----------------------------------------------------------------
printf "Random read and random flush     "
$RUN ./compress_rnd 16 15 256 8192 $TEST_FILE $OUT
$RUN ./decompress $TEST_FILE.comp $OUT
comp_ratio $TEST_FILE
diff $TEST_FILE $TEST_FILE.comp.decomp >/dev/null 2>&1
print_status $?
rm -rf  $TEST_FILE.comp.decomp $TEST_FILE.comp >/dev/null 2>&1
#----------------------------------------------------------------
printf "Edging                           "
$RUN ./comp_edge 16 15 256 8192 $TEST_FILE $OUT
$RUN ./decomp_edge $TEST_FILE.comp $OUT
comp_ratio $TEST_FILE
diff $TEST_FILE $TEST_FILE.comp.decomp >/dev/null 2>&1
print_status $?
rm -rf  $TEST_FILE.comp.decomp $TEST_FILE.comp >/dev/null 2>&1

printf "Edging2                           "
$RUN ./comp_edge1 16 15 50 259 $TEST_FILE $OUT
$RUN ./decomp_edge1 $TEST_FILE.comp $OUT
comp_ratio $TEST_FILE
diff $TEST_FILE $TEST_FILE.comp.decomp >/dev/null 2>&1
print_status $?
rm -rf  $TEST_FILE.comp.decomp $TEST_FILE.comp >/dev/null 2>&1

printf "Edging3                           "
$RUN ./comp_edge2 16 15 50 259 $TEST_FILE $OUT
$RUN ./decomp_edge2 $TEST_FILE.comp $OUT
comp_ratio $TEST_FILE
diff $TEST_FILE $TEST_FILE.comp.decomp >/dev/null 2>&1
print_status $?
rm -rf  $TEST_FILE.comp.decomp $TEST_FILE.comp >/dev/null 2>&1

#----------------------------------------------------------------
printf "Load existing edged cache        "
$RUN ./comp_loadcache 16 15 256 8192 ${TEST_FILE} $OUT
$RUN ./decomp_loadcache ${TEST_FILE}.comp $OUT
comp_ratio $TEST_FILE
diff ${TEST_FILE} ${TEST_FILE}.comp.decomp >/dev/null 2>&1
print_status $?
rm -rf  $TEST_FILE.comp.decomp $TEST_FILE.comp >/dev/null 2>&1
#----------------------------------------------------------------
printf "Compress small buffer(12B)       "
$RUN ./comp_smallbuff 16 15 256 8192 $TEST_FILE $OUT
$RUN ./decompress $TEST_FILE.comp $OUT
comp_ratio $TEST_FILE
diff $TEST_FILE $TEST_FILE.comp.decomp >/dev/null 2>&1
print_status $?
rm -rf  $TEST_FILE.comp.decomp $TEST_FILE.comp >/dev/null 2>&1
