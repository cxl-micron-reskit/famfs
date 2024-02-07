#!/usr/bin/bash

echo -n "pte faults: "
sudo cat /sys/fs/famfs/pte_fault_ct
echo
echo -n "pmd faults: "
sudo cat /sys/fs/famfs/pmd_fault_ct
echo
echo -n "pud faults: "
sudo cat /sys/fs/famfs/pud_fault_ct
echo
