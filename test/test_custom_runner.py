from platformio.public import UnityTestRunner

class CustomTestRunner(UnityTestRunner):

    # Ignore "throwtheswitch/Unity" package
    EXTRA_LIB_DEPS = None
    
    # Do not add default Unity to the build process
    def configure_build_env(self, env):
        pass