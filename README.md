## /dev/fanout
Kernel-based publish-subscribe system

## WHAT:
- simple, efficient, broadcast IPC, or alternatively
- simple, efficient, publish-subscribe IPC
- kernel module implementing a character device


## WHY:
- Twice as efficient:  Only N writes for N subscribers compared to 2*N
  writes for a sockets based system
- Simple: Kernel based, no need for a new process or daemon
- Simple: Use mknod to create a channel or topic as a device node
- Simple: API is just open()/read()/write()/close()
- Simple: Works with select() for event driven programming
- Simple: Works with ALL programming languages, even Bash
- Simple: No dependencies and no libraries to install
- Simple: Builds on all Linux systems
- Secure: Access rights tied to filesystem permissions on device node


## INSTALLATION:
    sudo apt-get install linux-headers-`uname -r`
    git clone --depth=1 https://github.com/bob-linuxtoys/fanout
    cd fanout
    make
    sudo make install


## TEST:
    sudo modprobe fanout   # load the kernel module, create 8 nodes
    # rename one of the nodes for our use of it
    sudo mv /dev/fanout7 /dev/fanouttest
    # Add three subscribers to the topic on /dev/fanouttest
    cat /dev/fanouttest &
    cat /dev/fanouttest &
    cat /dev/fanouttest &
    # Publish a messagte to fanouttest topic
    echo Hello, World > /dev/fanouttest
    

## NOTES:
See http://linustoys.org for an article on fanout.
See Linux Journal of August, 2010 for another article
See also http://github.org/bob-linuxtoys/proxy



## COPYRIGHT
Copyright 2010-2021, Bob Smith, Frederic Roussel



## LICENSE:
At your discretion, you may consider this software covered
by either the GNU Public License, Version 2, or the BSD
3-Clause license.
    http://opensource.org/licenses/GPL-2.0
    http://opensource.org/licenses/BSD-3-Clause

