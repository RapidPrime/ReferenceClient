const int CLIENT_VERSION = 1;
#ifdef WIN32
const int CLIENT_BUILD = 4001;
#else
const int CLIENT_BUILD = __BUILD_NUMBER;
#endif
const int PROTOCOL_VERSION = 0;
