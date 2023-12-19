# Famfs Usage

This docment is meant as a guide to getting started with using famfs.

# How is Famfs Difference From Conventional File Systems?

# Famfs Quirks and Limitations

* Must be root for 'famfs cp'. This is because creating files requires a log append, anad that
  requires root. Will be fixed eventually.
* Must be root for 'famfs mkdir'. Same log append issue.
* Sometimes 'make clean' requires sudo (because tests need sudo and leave artifacts around)
* Famfs mounts don't currently  show in 'df'. For now, 'grep famfs /proc/mounts'
