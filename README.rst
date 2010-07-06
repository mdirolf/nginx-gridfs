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

Directives
----------

**gridfs**

:syntax: *gridfs DB_NAME [root_collection=ROOT] [field=QUERY_FIELD] [type=QUERY_TYPE] [user=USERNAME] [pass=PASSWORD]* 
:default: *NONE*
:context: location

This directive enables the **nginx-gridfs** module at a given location. The 
only required parameter is DB_NAME to specify the database to serve files from. 

* *root_collection=* specify the root_collection(prefix) of the GridFS. default: *fs*
* *field=* specify the field to query. Supported fields include *_id* and *filename*. default: *_id*
* *type=* specify the type to query. Supported types include *objectid*, *string* and *int*. default: *objectid*
* *user=* specify a username if your mongo database requires authentication. default: *NULL*
* *pass=* specify a password if your mongo database requires authentication. default: *NULL*

**mongo**

:syntax: *mongo MONGOD_HOST*
:default: *127.0.0.1:27017*
:context: location

This directive specifies a mongod to connect to. MONGOD_HOST should be in the
form of hostname:port. This directive is optional.

Sample Configurations
---------------------

Here is a sample configuration in the relevant section of an *nginx.conf*::

  location /gridfs/ {
      gridfs my_app;
  }

This will set up Nginx to serve the file in gridfs with _id *ObjectId("a12...")*
for any request to */gridfs/a12...*

Here is another configuration::

  location /gridfs/ {
      gridfs my_app field=filename type=string;
      mongo 127.0.0.1:27017;
  } 

This will set up Nginx to serve the file in gridfs with filename *foo*
for any request to */gridfs/foo*

Here is another configuration::

  location /gridfs/ {
      gridfs my_app 
             root_collection=pics 
             field=_id 
             type=int
             user=foo 
             pass=bar;
      mongo 127.0.0.1:27017;
  } 

This will set up Nginx to communicate with the mongod at 127.0.0.1:27017 and 
authenticate use of database *my_app* with username/password combo *foo/bar*.
The gridfs root_collection is specified as *pics*. Nginx will then serve the 
file in gridfs with _id *123...* for any request to */gridfs/123...*

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
