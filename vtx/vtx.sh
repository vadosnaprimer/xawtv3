#!/bin/bash

if test ! -f 100_00.vtx; then
	echo "100_00.vtx not found, chdir to the station directory first"
	exit
fi

echo "keys:"
echo "  <number>return  jump to page <number>"
echo "  return          jump to next (sub-) page"
echo "  ^C              quit"
echo
echo "hit return to begin"
read key

current=100
sub="00"
timestamp="/tmp/.vtx-$$"
rm -rf $timestamp
set -o noclobber
> $timestamp || exit 1
set +o noclobber

cleanup() {
	rm -f $timestamp
}
trap cleanup EXIT

display() {
	touch $timestamp
	echo -e "\033[H"
	vtx2ascii -c "${current}_${sub}.vtx"
	echo
	echo -n "$current"
	test "$sub" != "00" && echo -n "/$sub"
	echo -n "> "
}

alarm() {
	if test "${current}_${sub}.vtx" -nt $timestamp; then
		display
	fi
}
trap alarm SIGALRM || exit
(while kill -SIGALRM $$; do sleep 1; done)&

while true; do
	clear
	test ! -f "${current}_${sub}.vtx" && sub="00";
	test ! -f "${current}_${sub}.vtx" && sub="01";
	display
	while true; do read new && break; done
	if test "$new" = ""; then
		if test "$sub" != "00"; then
			sub=`printf '%02d' $[$sub+1]`;
		else
			current=`printf '%02d' $[$current+1]`;
		fi
	else
		current="$new"
		sub="00"
	fi
done

