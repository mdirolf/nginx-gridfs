nginx-gridfs
============
:Author: Mike Dirolf <mike@dirolf.com>

About
=====
**nginx-gridfs** is an `Nginx <http://nginx.net/>`_ module to serve
content directly from `MongoDB <http://www.mongodb.org/>`_'s `GridFS
<http://www.mongodb.org/display/DOCS/GridFS>`_.

Dependencies
============
**nginx-gridfs** requires the MongoDB C++ client library (which is
installed by default when MongoDB is installed). It also needs to link
against the `boost libraries <http://www.boost.org/>`_, since the
MongoDB client depends on them.

Installation
============
Installing Nginx modules requires rebuilding Nginx from source:

* Grab the `Nginx source <http://nginx.net/>`_ and unpack it.
* Clone this repository somewhere on your machine.
* Change to the directory containing the Nginx source.
* Set environment variables::

    $ export MONGO_INCLUDE_PATH=/path/to/mongodb/includes/
    $ export LIBMONGOCLIENT=/path/to/libmongoclient/file
    $ export BOOST_INCLUDE_PATH=/path/to/boost/includes/
    $ export LIBBOOST_THREAD=/path/to/libboost_thread/file
    $ export LIBBOOST_FILESYSTEM=/path/to/libboost_filesystem/file

  Ideally there will be a better way to do this eventually, but this
  is easy.

* Now build::

    $ ./configure --add-module=/path/to/nginx-gridfs/source/
    $ make
    $ make install

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
can be specified but default to the values given in the configuration
above.

License
=======
**nginx-gridfs** is licensed under the Apache License, Version 2.0. See *LICENSE* for details.
