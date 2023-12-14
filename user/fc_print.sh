#!/usr/bin/bash

echo -n "pte faults: "
sudo cat /sys/kernel/famfs/pte_fault_ct
echo
echo -n "pmd faults: "
sudo cat /sys/kernel/famfs/pmd_fault_ct
echo
echo -n "pud faults: "
sudo cat /sys/kernel/famfs/pud_fault_ct
echo
