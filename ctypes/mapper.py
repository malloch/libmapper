from ctypes import *

# need different library extensions for Linux, Windows
cdll.LoadLibrary("libmapper-0.dylib")
mapper = CDLL("libmapper-0.dylib")

class TIMETAG(Structure):
    _fields_ = [("sec", c_ulong),
                ("frac", c_ulong)]

class MAPPER_SIGNAL_VALUE(Union):
    _fields_ = [("f", c_float),
                ("d", c_double),
                ("i32", c_int)]

class STRING_TABLE_NODE(Structure):
    _fields_ = [("key", c_char_p),
                ("value", c_void_p)]

class MAPPER_STRING_TABLE(Structure):
    _fields_ = [("store", POINTER(STRING_TABLE_NODE)),
                ("len", c_int),
                ("alloced", c_int)]

class device_props(Structure):
    _fields_ = [("identifier", c_char_p),
                ("name", c_char_p),
                ("ordinal", c_int),
                ("name_hash", c_uint),
                ("host", c_char_p),
                ("port", c_int),
                ("n_inputs", c_int),
                ("n_outputs", c_int),
                ("n_links_in", c_int),
                ("n_links_out", c_int),
                ("n_connections_in", c_int),
                ("n_connections_out", c_int),
                ("version", c_int),
                ("user_data", c_void_p),
                ("timetag", TIMETAG),
                ("synced", TIMETAG),
                ("extra", POINTER(MAPPER_STRING_TABLE))]

class signal_props(Structure):
    _fields_ = [("id", c_int),
                ("is_output", c_int),
                ("type", c_char),
                ("length", c_int),
                ("num_instances", c_int),
                ("history_size", c_int),
                ("name", c_char_p),
                ("device_name", c_char_p),
                ("unit", c_char_p),
                ("minimum", MAPPER_SIGNAL_VALUE),
                ("maximum", MAPPER_SIGNAL_VALUE),
                ("rate", c_float),
                ("extra", POINTER(MAPPER_STRING_TABLE)),
                ("user_data", c_void_p)]

SIGNAL_HANDLER = CFUNCTYPE(None, c_void_p, POINTER(signal_props), c_int, c_void_p, POINTER(TIMETAG))

class device:
    def __init__(self, name, port=0, admin=0):
        mapper.mdev_new.restype = c_void_p
        self.device = c_void_p(mapper.mdev_new(name, port, admin))
        self.ready = False
        self.name = None
        self.ordinal = None
        self.num_inputs = 0
        self.num_outputs = 0
        self.interface = None
        
    def __del__(self):
        mapper.mdev_free(self.device)

    def poll(self, timeout=0):
        mapper.mdev_poll(self.device, timeout)
        if not self.ready and mapper.mdev_ready(self.device):
            self.ready = True
            self.name = mapper.mdev_name(self.device)
            self.ordinal = mapper.mdev_ordinal(self.device)
            self.interface = mapper.mdev_interface(self.device)

    def ready(self):
        return mapper.mdev_ready(self.device)

    def add_input(self, name, length=1, type='f', unit=None, minimum=None, maximum=None, callback=0):
        cb_wrap = SIGNAL_HANDLER(callback)
        mapper.mdev_add_input.argtypes = [c_void_p, c_char_p, c_int, c_char, c_char_p, c_void_p, c_void_p, c_void_p, c_void_p]
        mapper.mdev_add_input.restype = c_void_p
        msig = c_void_p(mapper.mdev_add_input(self.device, name, length, type, unit, None, None, cb_wrap, 0))
        if not msig:
            return None

        if minimum:
            if type == 'i':
                mapper.msig_set_minimum(msig, byref(c_int(int(minimum))))
            elif type == 'f':
                mapper.msig_set_minimum(msig, byref(c_float(float(minimum))))
            elif type == 'd':
                mapper.msig_set_minimum(msig, byref(c_double(double(minimum))))
        if maximum:
            if type == 'i':
                mapper.msig_set_maximum(msig, byref(c_int(int(maximum))))
            elif type == 'f':
                mapper.msig_set_maximum(msig, byref(c_float(float(maximum))))
            elif type == 'd':
                mapper.msig_set_maximum(msig, byref(c_double(double(maximum))))

        pysig = signal(msig)
        self.num_inputs = mapper.mdev_num_inputs(self.device)
        return pysig

    def add_output(self, name, length=1, type='f', unit=0, minimum=None, maximum=None):
        mapper.mdev_add_output.argtypes = [c_void_p, c_char_p, c_int, c_char, c_char_p, c_void_p, c_void_p]
        mapper.mdev_add_output.restype = c_void_p
        msig = c_void_p(mapper.mdev_add_output(self.device, name, length, type, unit, None, None))
        if not msig:
            return None

        if minimum:
            if type == 'i':
                mapper.msig_set_minimum(msig, byref(c_int(int(minimum))))
            elif type == 'f':
                mapper.msig_set_minimum(msig, byref(c_float(float(minimum))))
            elif type == 'd':
                mapper.msig_set_minimum(msig, byref(c_double(double(minimum))))
        if maximum:
            if type == 'i':
                mapper.msig_set_maximum(msig, byref(c_int(int(maximum))))
            elif type == 'f':
                mapper.msig_set_maximum(msig, byref(c_float(float(maximum))))
            elif type == 'd':
                mapper.msig_set_maximum(msig, byref(c_double(double(maximum))))

        pysig = signal(msig)
        self.num_outputs = mapper.mdev_num_outputs(self.device)
        return pysig

    def remove_input(self, signal):
        mapper.mdev_remove_input(self.device, signal)
        self.num_inputs = mapper.mdev_num_inputs(self.device)

    def remove_output(self, signal):
        mapper.mdev_remove_output(self.device, signal)
        self.num_outputs = mapper.mdev_num_outputs(self.device)

    def get_input_by_name(self, name):
        sig = mapper.mdev_get_input_by_name(self.device, name, 0)
        return sig

    def get_output_by_name(self, name):
        sig = mapper.mdev_get_output_by_name(self.device, name, 0)
        return sig

    def get_input_by_index(self, index):
        sig = mapper.mdev_get_input_by_index(self.device, index)
        return sig

    def get_output_by_index(self, index):
        sig = mapper.mdev_get_output_by_index(self.device, index)
        return sig

    def set_property(self):
        pass

    def get_property(self):
        pass

    def remove_property(self):
        pass

    def now():
        return mapper.mapper_timetag_get_double(mapper.mdev_now())

    def start_queue(self, timetag):
        pass

    def send_queue(self, timetag):
        pass

    def set_link_callback(self, callback):
        pass

    def set_connection_callback(self, callback):
        pass

class signal:
    def __init__(self, ref):
        self.signal = ref
        #init some props
        mapper.msig_properties.restype = POINTER(signal_props)
        self.props = mapper.msig_properties(ref)
        print 'signal props:', self.props.contents.minimum

    def update(self, value, timetag=0):
        tt = TIMETAG(0, 1)
        if timetag:
            mapper.mapper_timetag_set_double(byref(tt), timetag)
        count = c_int(1)
        if type(value) == list:
            if len(value) % self.props.contents.length != 0:
                print 'error: bad list length in signal update!'
                return
            count = c_int(len(value) / self.props.contents.length)
            if self.props.contents.type == 'i':
                coerced = [ int(x) for x in value ]
                converted = (c_int * len(coerced))(*coerced)
            elif self.props.contents.type == 'f':
                coerced = [ float(x) for x in value ]
                converted = (c_float * len(coerced))(*coerced)
            elif self.props.contents.type == 'd':
                coerced = [ double(x) for x in value ]
                converted = (c_double * len(coerced))(*coerced)
        else:
            if self.props.contents.type == 'i':
                converted = c_int(int(value))
            elif self.props.contents.type == 'f':
                converted = c_float(float(value))
            elif self.props.contents.type == 'd':
                converted = c_double(double(value))
        mapper.msig_update(self.signal, converted, count, tt)


#class monitor():
#class db():
