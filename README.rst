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

Version
============
The minor version will be incremented with each release until
a stable 1.0 is reached. To check out a particular version::

    $ git checkout v0.8

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

When connecting to a single server::

:syntax: *mongo MONGOD_HOST*
:default: *127.0.0.1:27017*
:context: location

When connecting to a replica set::

:syntax: *mongo REPLICA_SET_NAME* *MONGOD_SEED_1* *MONGOD_SEED_2*
:default: *127.0.0.1:27017*
:context: location

This directive specifies a mongod or replica set to connect to. MONGOD_HOST should be in the
form of hostname:port. REPLICA_SET_NAME should be the name of the replica set to connect to.

If this directive is not provided, the module will attempt to connect to a MongoDB server at *127.0.0.1:27017*.

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

Here's how to connect to a replica set called "foo" with two seed nodes::

  location /gridfs/ {
      gridfs my_app field=filename type=string;
      mongo "foo"
            10.7.2.27:27017
            10.7.2.28:27017;
  }

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

Multiple versions of the files
===============================

**nginx-gridfs** allows you to serve multiple versions of the same file based on a query parameter.
This is useful, for instance, when you want to serve images of different sizes and/or quality.

To use this, you just have to provide a *type=<value>* query parameter when requesting the file.
**nginx-gridfs** will pick it up automatically, append the *value* to the filename with an underscore 
(_) in between and serve that particular version of the file.

**NOTE** : 
* This can be used only if the field type that is being queried on is String. This will not work with ObjectId and Integer fields.
* The name of the query parameter **must** be *type*.
* The additional versions of the file **must** follow the convention of naming the file as *filename_<version>*

Examples
---------

Assuming you have the following configuration to serve images from GridFS::

  location /gridfs/ {
      gridfs my_app field=filename type=string;
      mongo 127.0.0.1:27017;
  }

and you two extra versions of the images named *small* and *mid*.

* /gridfs/foo will serve the default version of the file with the filename *foo*
* /gridfs/foo?type=small will serve the "small" version of the file with the filename *foo_small*
* /gridfs/foo?type=mid will serve the "mid" version of the file with the filename *foo_mid*
* /gridfs/foo?type=not-existing will serve the default of the file with the filename *foo* - Read the next secion for details.


Fallback to default version
----------------------------

If a "type" is specified and that particular version of the file is not found, then *nginx-gridfs* will
fallback to the default version of the file and serve that instead. This is particularly useful if you
are generating the additional versions of the files asynchronously and all the versions may not always
be available. This will also allow you to discard any unknown versions requested and serve the default
version for all those requests.

In the above examples if */gridfs/foo?type=small* is requested and the file *foo_small* is not present
in GridFS, **nginx-gridfs** will instead serve the default version of the file *foo*.

Later when the *foo_small* version of the file is added, any subsequent request for */gridfs/foo?type=small*
will be served the small version.

Known Issues / TODO / Things You Should Hack On
===============================================

* HTTP range support for partial downloads
* Better error handling / logging

Credits
=======

* Sho Fukamachi (sho) - towards compatibility with newer boost versions
* Olivier Bregeras (stunti) - better handling of binary content
* Chris Heald (cheald) - better handling of binary content
* Paul Dlug (pdlug) - mongo authentication
* Todd Zusman (toddzinc) - gzip handling
* Kyle Banker (banker) - replica set support

License
=======
**nginx-gridfs** is dual licensed under the Apache License, Version
2.0 and the GNU General Public License, either version 2 or (at your
option) any later version. See *LICENSE* for details.
