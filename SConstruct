# -*- mode: python; -*-
import os

Object("gridfs_c_helpers.cpp",
       CPPPATH=[os.environ.get("MONGO_INCLUDE_PATH"), os.environ.get("BOOST_INCLUDE_PATH")],
       LIBS=[os.environ.get("LIBMONGOCLIENT"),
             os.environ.get("LIBBOOST_THREAD"),
             os.environ.get("LIBBOOST_FILESYSTEM")])

