'''
This is a script that generates coverate reports when tests run for the native platform.
'''

import os

Import("env")

env.Append(
  LINKFLAGS=[
      "--coverage"
  ]
)


def generateCoverageInfo(source, target, env):
    for file in os.listdir("test"):
        os.system(".pio/build/native/program test/"+file)
    os.system("lcov -d .pio/build/native/ -c -o cov/lcov.info")
    os.system("lcov --remove cov/lcov.info '*/tool-unity/*' '*/.pio/*' '*/Applications/Xcode.app/*' '*/test/*' '*/MockArduino/*' -o cov/filtered_lcov.info")
    os.system("genhtml -o cov/ --demangle-cpp cov/filtered_lcov.info")

env.AddPostAction(".pio/build/native/program", generateCoverageInfo)