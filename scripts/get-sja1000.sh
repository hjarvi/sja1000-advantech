#!/bin/sh

commit=v5.4

cd drivers/net/can/sja1000 || exit 1
if [ -e "sja1000.h" ]; then exit 0; fi
wget "https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/plain/drivers/net/can/sja1000/sja1000.h?h=$commit" -O sja1000.h
sha256sum --check sja1000.h.sha256
if [ "$?" != 0 ]; then
  rm -f sja1000.h
  exit 1
fi
