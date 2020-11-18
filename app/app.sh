#!/bin/bash

DEFAULT_CONFIG_DIR=/usr/src/app/
CONFIG_DIR=/etc/fieldbus/

if [ ! -f $CONFIG_DIR/config.json ]; then
    echo "config does not exist. copy it!"
    cp $DEFAULT_CONFIG_DIR/config.json $CONFIG_DIR/config.json
    cp $DEFAULT_CONFIG_DIR/protocol.db $CONFIG_DIR/protocol.db
else
    echo "config exist!"
fi
# *******************************************************************************
#  fieldbus controller start commands
# *******************************************************************************
exec /usr/local/sbin/fbcontroller
