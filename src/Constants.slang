public module Constants;

static const float EPSILON = 0.01;
static const float PI = 3.14159265359;
static const uint32_t UINT32_MAX = 4294967295;
static const int RAY_COUNT = 192;
static const float HYSTERESIS = 0.85;
static const int PROBES_PER_CASCADE = 5445;
static const int DDGI_TEXTURE_SIZE = int(ceil(sqrt(PROBES_PER_CASCADE)));
static const float MINIMUM_RAY_DISTANCE = EPSILON * 5;
