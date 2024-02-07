# Famfs Usage

This docment is meant as a guide to getting started with using famfs.

# How is Famfs Different From Conventional File Systems?
TBD

# Famfs Quirks and Limitations

* Currently famfs only fully works on /dev/pmem devices. This is due to the fact that
  dax_iomap_[fault|rw] are only properly plumbed for pmem. The dev_dax_patch series,
  once it fully works, will address this and be incorporated into the famfs patch series.
* Must be root for 'famfs cp', 'famfs mkdir', etc..
  This is because creating files requires a log append, and that
  requires root. It's also because getting the system uuid (to determine whether your on the
  system that can write the log) requires root. Will be fixed eventually.
* Must be root for 'famfs mkdir'. Same log append issue.
* Sometimes 'make clean' requires sudo (because tests need sudo and leave artifacts around)
* Famfs mounts don't currently  show in 'df'. For now, 'grep famfs /proc/mounts'
* To run the smoke tests, you need password-less sudo enabled. Sorry for the inconvenience.
