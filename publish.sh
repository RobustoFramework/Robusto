#!/bin/bash

# Publish Robusto framework and component
# HOWTO:
# * Commit all changes
# * Update version in components/robusto/idf_component.yml and library.json
# * Create a commit with message "Tick up or similar
# * Run this script
# * Create a tag with the same version as in idf_component.yml and library.json
pio pkg publish --type library --owner robusto
cd components/robusto/
compote component upload --namespace robusto --name Robusto
cd ../../