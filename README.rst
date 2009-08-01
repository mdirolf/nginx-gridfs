nginx-gridfs
============
:Author: Mike Dirolf <mike@dirolf.com>

About
=====
nginx-gridfs is an Nginx module to serve content directly from MongoDB's GridFS.

Installation
============
This is a bit of a work in progress.

Configuration
=============
Here is the relevant section of an *nginx.conf*::

  location /gridfs/ {
      gridfs;
      gridfs_db my_app;

      # these are the default values:
      mongod_host 127.0.0.1:27017;
      gridfs_root_collection fs;
  }

The only required configuration variables are **gridfs** to enable the
module for this location and **gridfs_db** to specify the database in
which to store files. **mongod_host** and **gridfs_root_collection**
can be specified but default to the values given in the configuration above.
