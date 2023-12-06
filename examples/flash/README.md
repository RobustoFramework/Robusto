# The flash examples
These examples uses the Robusto implementations to writes and read from different flash storages

## Internal - SPIFFS example
Shows writing and reading from internal flash using the Robusto SPIFFS support.
Note that the min_spiff.csv in Robusto has multiple partitions and a designated spiffs-partition. 
Note also that PlatformIO do not support multiple partitions when flashing the partition table.
You will have to use idf.py partition-table-flash to reupload it.

## External - Flash example
Not implemented
