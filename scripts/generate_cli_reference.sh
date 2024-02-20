#!/bin/env bash

OUTFILE="cli-ref.md"
$Q='```'

if [ -z "$1" ]; then
    echo "Sending output to $OUTFILE"
fi

rm $OUTFILE
touch $OUTFILE

# mkfs.famfs
echo "# mkfs.famfs"   >> $OUTFILE
echo '```'            >> $OUTFILE
mkfs.famfs -h         >> $OUTFILE
echo '```'            >> $OUTFILE

echo "# The famfs cli"    >> $OUTFILE
echo "The famfs CLI enables most of the normal maintenance operations with famfs."  >> $OUTFILE
echo                  >> $OUTFILE
echo '```'            >> $OUTFILE
famfs -h              >> $OUTFILE
echo '```'            >> $OUTFILE
echo                  >> $OUTFILE

echo "## famfs mount" >> $OUTFILE
echo '```'            >> $OUTFILE
famfs mount -?        >> $OUTFILE
echo '```'            >> $OUTFILE

echo "## famfs fsck"  >> $OUTFILE
echo '```'            >> $OUTFILE
famfs fsck -?         >> $OUTFILE
echo '```'            >> $OUTFILE

echo "## famfs check" >> $OUTFILE
echo '```'            >> $OUTFILE
famfs check -?        >> $OUTFILE
echo '```'            >> $OUTFILE

echo "## famfs mkdir" >> $OUTFILE
echo '```'            >> $OUTFILE
famfs mkdir -?        >> $OUTFILE
echo '```'            >> $OUTFILE

echo "## famfs cp"    >> $OUTFILE
echo '```'            >> $OUTFILE
famfs cp -?           >> $OUTFILE
echo '```'            >> $OUTFILE

echo "## famfs creat" >> $OUTFILE
echo '```'            >> $OUTFILE
famfs creat -?        >> $OUTFILE
echo '```'            >> $OUTFILE

echo "## famfs verify" >> $OUTFILE
echo '```'             >> $OUTFILE
famfs verify -?        >> $OUTFILE
echo '```'             >> $OUTFILE

echo "## famfs mkmeta" >> $OUTFILE
echo '```'             >> $OUTFILE
famfs mkmeta -?        >> $OUTFILE
echo '```'             >> $OUTFILE

echo "## famfs logplay" >> $OUTFILE
echo '```'              >> $OUTFILE
famfs logplay -?        >> $OUTFILE
echo '```'              >> $OUTFILE

echo "## famfs getmap"  >> $OUTFILE
echo '```'              >> $OUTFILE
famfs getmap -?         >> $OUTFILE
echo '```'              >> $OUTFILE

echo "## famfs clone"   >> $OUTFILE
echo '```'              >> $OUTFILE
famfs clone -?          >> $OUTFILE
echo '```'              >> $OUTFILE

echo "## famfs chkread" >> $OUTFILE
echo '```'              >> $OUTFILE
famfs chkread -?        >> $OUTFILE
echo '```'              >> $OUTFILE




