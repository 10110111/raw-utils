#!/bin/sh

# NOTE: to use local API description, unpack gl.xml.xz so that glad sees gl.xml in its $PWD
cd "`dirname $0`/glad"
glad --out-path=. --generator=c --omit-khrplatform --api="gl=3.3" --profile=core --extensions=
cd -
