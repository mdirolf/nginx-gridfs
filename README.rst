nginx-gridfs
============
:Authors:
    Mike Dirolf <mike@dirolf.com>,
    Chris Triolo,
    and everyone listed in the Credits section below

About
=====
**nginx-gridfs** is an `Nginx <http://nginx.net/>`_ module to serve
content directly from `MongoDB <http://www.mongodb.org/>`_'s `GridFS
<http://www.mongodb.org/display/DOCS/GridFS>`_.

Dependencies
============
**nginx-gridfs** requires the Mongo-C-Driver which is a submodule to
this repository. To check out the submodule (after cloning this
repository), run::

    $ git submodule init
    $ git submodule update

Installation
============
Installing Nginx modules requires rebuilding Nginx from source:

* Grab the `Nginx source <http://nginx.net/>`_ and unpack it.
* Clone this repository somewhere on your machine.
* Check out the required submodule, as described above.
* Change to the directory containing the Nginx source.
* Now build::

    $ ./configure --add-module=/path/to/nginx-gridfs/source/
    $ make
    $ make install

Configuration
=============
Here is the relevant section of an *nginx.conf*::

  location /gridfs/ {
      gridfs_db my_app;

      # these are the default values:
      mongod_host 127.0.0.1:27017;
      mongod_user user; 
      mongod_pass pass;
      gridfs_root_collection fs;
      gridfs_field _id; # Supported {_id, filename} 
      gridfs_type objectid; # Supported {objectid, string, int}
  }

The only required configuration variables is **gridfs_db** to enable
the module for a location and to specify the database in which to
store files. **mongod_host** and **gridfs_root_collection** can be
specified but default to the values given in the configuration above.

This will set up Nginx to serve the file in gridfs with _id *ObjectId("a12...")*
for any request to */gridfs/a12...*

Known Issues / TODO / Things You Should Hack On
===============================================

* Some issues with large files
* HTTP range support for partial downloads
* Better error handling / logging

Credits
=======

* Sho Fukamachi (sho) - towards compatibility with newer boost versions
* Olivier Bregeras (stunti) - better handling of binary content
* Chris Heald (cheald) - better handling of binary content
* Paul Dlug (pdlug) - mongo authentication

License
=======
**nginx-gridfs** is dual licensed under the Apache License, Version
2.0 and the GNU General Public License, either version 2 or (at your
option) any later version. See *LICENSE* for details.
