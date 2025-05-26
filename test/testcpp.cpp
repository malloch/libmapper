
#include <cstring>
#include <iostream>
#include <cstdio>
#include <cstdlib>
#include <array>
#include <signal.h>

#include <mapper/mapper_cpp.h>

#ifdef HAVE_ARPA_INET_H
 #include <arpa/inet.h>
#endif

using namespace mapper;

int received = 0;
int done = 0;

int verbose = 1;
int autoquit = 0;
int period = 100;

class out_stream : public std::ostream {
public:
    out_stream() : std::ostream (&buf) {}

private:
    class null_out_buf : public std::streambuf {
    public:
        virtual std::streamsize xsputn (const char * s, std::streamsize n) {
            return n;
        }
        virtual int overflow (int c) {
            return 1;
        }
    };
    null_out_buf buf;
};
out_stream null_out;

class Custom {
public:
    Custom(const char* str) {
        extra = strdup(str);
    }
    ~Custom() {
        if (extra)
            free((void*)extra);
    }
    const char* extra;
};

void simple_handler(Signal&& sig, int length, Type type, const void *value, Time&& t)
{
    ++received;
    if (verbose) {
        std::cout << "\t\t\t\t     | --> simple handler: " << sig[Property::NAME] << " got";
    }

    if (!value) {
        if (verbose)
            std::cout << " NULL" << std::endl;
        return;
    }
    else if (!verbose) {
        return;
    }

    switch (type) {
        case Type::INT32: {
            int *v = (int*)value;
            for (int i = 0; i < length; i++) {
                std::cout << " " << v[i];
            }
            break;
        }
        case Type::FLOAT: {
            float *v = (float*)value;
            for (int i = 0; i < length; i++) {
                std::cout << " " << v[i];
            }
            break;
        }
        case Type::DOUBLE: {
            double *v = (double*)value;
            for (int i = 0; i < length; i++) {
                std::cout << " " << v[i];
            }
            break;
        }
        default:
            break;
    }
    std::cout << std::endl;
}

void standard_handler(Signal&& sig, Signal::Event event, Id instance, int length,
                      Type type, const void *value, Time&& t)
{
    ++received;
    if (verbose) {
        std::cout << "\t\t\t\t     | --> standard handler: " << sig[Property::NAME] << "." << instance
                  << " got";
    }

    if (!value) {
        if (verbose)
            std::cout << " release" << std::endl;
        sig.instance(instance).release();
        return;
    }
    else if (!verbose) {
        return;
    }

    switch (type) {
        case Type::INT32: {
            int *v = (int*)value;
            for (int i = 0; i < length; i++) {
                std::cout << " " << v[i];
            }
            break;
        }
        case Type::FLOAT: {
            float *v = (float*)value;
            for (int i = 0; i < length; i++) {
                std::cout << " " << v[i];
            }
            break;
        }
        case Type::DOUBLE: {
            double *v = (double*)value;
            for (int i = 0; i < length; i++) {
                std::cout << " " << v[i];
            }
            break;
        }
        default:
            break;
    }
    std::cout << std::endl;

    // also test recovering arbitrary use data
    void *data = sig[Property::DATA];
    if (data) {
        const Custom *custom = reinterpret_cast<const Custom*>(data);
        std::cout <<  "\t\t\t\t     |     recovered user_data: " << custom->extra << std::endl;
    }
}

void instance_handler(Signal::Instance&& si, Signal::Event event, int length,
                      Type type, const void *value, Time&& t)
{
    ++received;
    if (verbose) {
        std::cout << "\t\t\t\t     | --> instance handler: " << si.signal()[Property::NAME] << "."
                  << si.id() << " got " << event;
    }

    if (event == Signal::Event::REL_UPSTRM) {
        if (verbose)
            std::cout << " release" << std::endl;
        si.release();
        return;
    }
    else if (!verbose || !value) {
        return;
    }

    switch (type) {
        case Type::INT32: {
            int *v = (int*)value;
            for (int i = 0; i < length; i++) {
                std::cout << " " << v[i];
            }
            break;
        }
        case Type::FLOAT: {
            float *v = (float*)value;
            for (int i = 0; i < length; i++) {
                std::cout << " " << v[i];
            }
            break;
        }
        case Type::DOUBLE: {
            double *v = (double*)value;
            for (int i = 0; i < length; i++) {
                std::cout << " " << v[i];
            }
            break;
        }
        default:
            break;
    }
    std::cout << std::endl;
}

void ctrlc(int sig)
{
    done = 1;
}

int main(int argc, char ** argv)
{
    int i = 0, j, result = 0;
    char *iface = 0;

    // process flags for -v verbose, -t terminate, -h help
    for (i = 1; i < argc; i++) {
        if (argv[i] && argv[i][0] == '-') {
            int len = (int)strlen(argv[i]);
            for (j = 1; j < len; j++) {
                switch (argv[i][j]) {
                    case 'h':
                        printf("testcpp.cpp: possible arguments "
                               "-q quiet (suppress output), "
                               "-t terminate automatically, "
                               "-f fast (execute quickly), "
                               "-h help,"
                               "--iface network interface\n");
                        return 1;
                        break;
                    case 'q':
                        verbose = 0;
                        break;
                    case 'f':
                        period = 1;
                        break;
                    case 't':
                        autoquit = 1;
                        break;
                    case '-':
                        if (strcmp(argv[i], "--iface")==0 && argc>i+1) {
                            i++;
                            iface = argv[i];
                            j = 1;
                        }
                        break;
                    default:
                        break;
                }
            }
        }
    }

    signal(SIGINT, ctrlc);

    std::ostream& out = verbose ? std::cout : null_out;

    Device dev("testcpp");
    if (iface)
        dev.graph().set_iface(iface);
    out << "Created Device with interface " << dev.graph().iface() << std::endl;

    // make a copy of the device to check reference counting
    Device devcopy(dev);

    Signal sig = dev.add_signal(Direction::INCOMING, "in1", 1, Type::FLOAT, "meters")
                    .set_callback(standard_handler);
    dev.remove_signal(sig);
    dev.add_signal(Direction::INCOMING, "in2", 2, Type::INT32).set_callback(standard_handler);
    dev.add_signal(Direction::INCOMING, "in3", 2, Type::INT32).set_callback(standard_handler);
    dev.add_signal(Direction::INCOMING, "in4", 2, Type::INT32).set_callback(simple_handler);

    sig = dev.add_signal(Direction::OUTGOING, "out1", 1, Type::FLOAT, "na");
    dev.remove_signal(sig);
    sig = dev.add_signal(Direction::OUTGOING, "out2", 3, Type::DOUBLE, "meters");

    out << "waiting" << std::endl;
    while (!dev.ready() && !done) {
        dev.poll(10);
    }
    out << "ready" << std::endl;

    out << "device " << dev[Property::NAME] << " ready..." << std::endl;
    out << "  ordinal: " << dev["ordinal"] << std::endl;
    out << "  id: " << dev[Property::ID] << std::endl;
    out << "  interface: " << dev.graph().iface() << std::endl;
    out << "  bus url: " << dev.graph().address() << std::endl;
    out << "  port: " << dev["port"] << std::endl;
    out << "  num_inputs: " << dev.signals(Direction::INCOMING).size() << std::endl;
    out << "  num_outputs: " << dev.signals(Direction::OUTGOING).size() << std::endl;
    out << "  num_incoming_maps: " << dev.maps(Direction::INCOMING).size() << std::endl;
    out << "  num_outgoing_maps: " << dev.maps(Direction::OUTGOING).size() << std::endl;

    int value[] = {1,2,3,4,5,6};
    dev.set_property("foo", 6, value);
    out << "foo: " << dev["foo"] << std::endl;

    dev["foo"] = 100;
    out << "foo: " << dev["foo"] << std::endl;

    // test std::array<std::string>
    out << "set and get std::array<std::string>: ";
    std::array<std::string, 3> a1 = {{"one", "two", "three"}};
    dev["foo"] = a1;
    const std::array<std::string, 8> a2 = dev["foo"];
    for (i = 0; i < 8; i++)
        out << a2[i] << " ";
    out << std::endl;

    // test std::array<const char*>
    out << "set and get std::array<const char*>: ";
    std::array<const char*, 3> a3 = {{"four", "five", "six"}};
    dev["foo"] = a3;
    std::array<const char*, 3> a4 = dev["foo"];
    for (i = 0; i < (int)a4.size(); i++)
        out << a4[i] << " ";
    out << std::endl;

    // test plain array of const char*
    out << "set and get const char*[]: ";
    const char* a5[3] = {"seven", "eight", "nine"};
    dev.set_property("foo", 3, a5);
    const char **a6 = dev["foo"];
    out << a6[0] << " " << a6[1] << " " << a6[2] << std::endl;

    // test plain array of float
    out << "set and get float[]: ";
    float a7[3] = {7.7f, 8.8f, 9.9f};
    dev.set_property("foo", 3, a7);
    const float *a8 = dev["foo"];
    out << a8[0] << " " << a8[1] << " " << a8[2] << std::endl;

    // test std::vector<const char*>
    out << "set and get std::vector<const char*>: ";
    const char *a9[3] = {"ten", "eleven", "twelve"};
    std::vector<const char*> v1(a9, std::end(a9));
    dev["foo"] = v1;
    std::vector<const char*> v2 = dev["foo"];
    out << "foo: ";
    for (std::vector<const char*>::iterator it = v2.begin(); it != v2.end(); ++it)
        out << *it << " ";
    out << std::endl;

    // test std::vector<std::string>
    out << "set and get std::vector<std::string>: ";
    const char *a10[3] = {"thirteen", "14", "15"};
    std::vector<std::string> v3(a10, std::end(a10));
    dev["foo"] = v3;
    std::vector<std::string> v4 = dev["foo"];
    out << "foo: ";
    for (std::vector<std::string>::iterator it = v4.begin(); it != v4.end(); ++it)
        out << *it << " ";
    out << std::endl;

    dev.remove_property("foo");
    out << "foo: " << dev["foo"] << " (should be 0x0)" << std::endl;

    // test 'data' property
    Signal sig2 = dev.signals(Direction::INCOMING)[1];
    Custom custom("Raspberry Beret");
    out << "adding Custom object '" << custom.extra << "' to " << sig2 << std::endl;
    sig2.set_property(Property::DATA, (void*)&custom);

    out << "try printing all device properties:" << std::endl;
    for (int i = 0; i < dev.num_props(); i++) {
        PropVal p = dev[i];
        out << "  {" << p.key << ": " << p << "}\t" << (p.publish ? "public" : "local") << std::endl;
    }

    out << "signal: " << sig << std::endl;

    out << "try printing all signal properties:" << std::endl;
    for (int i = 0; i < sig.num_props(); i++) {
        PropVal p = sig[i];
        out << "  {" << p.key << ": " << p << "}\t" << (p.publish ? "public" : "local") << std::endl;
    }

    out << "try printing all device signals:" << std::endl;
    List<Signal> qsig = dev.signals(Direction::INCOMING);
    qsig.begin();
    for (; qsig != qsig.end(); ++qsig) {
        out << "  input: " << *qsig << std::endl;
    }

    Graph graph;
    if (iface)
        graph.set_iface(iface);
    out << "Created Graph with interface " << graph.iface() << std::endl;
    Map map(dev.signals(Direction::OUTGOING)[0], dev.signals(Direction::INCOMING)[1]);
    map[Property::EXPRESSION] = "y=x[0:1]+123";

    map.push();

    while (!map.ready() && !done) {
        dev.poll(10);
    }

    // try using threaded device polling
    dev.start();

    std::vector <double> v(3);
    i = 0;
    while (i++ < 100 && !done) {
        v[i%3] = i;
        if (i == 50) {
            Signal s = *dev.signals().filter(Property::NAME, "in4");
            s.set_callback(standard_handler);
        }
        if (verbose)
            std::cout << "    Updating " << sig[Property::NAME]
                      << " to " << i <<  " \t-->  |" << std::endl;
        sig.set_value(v);
        graph.poll(period);
    }
    dev.stop();

    // try retrieving linked devices
    out << "devices linked to " << dev << ":" << std::endl;
    List<Device> foo = dev[Property::LINKED];
    for (; foo != foo.end(); foo++) {
        out << "  " << *foo << std::endl;
    }

    // try combining queries
    out << "devices with name matching 'my*' AND >=0 inputs" << std::endl;
    List<Device> qdev = graph.devices();
    qdev.filter(Property::NAME, "my*", Operator::EQUAL);
    qdev.filter(Property::NUM_SIGNALS_IN, 0, Operator::GREATER_THAN_OR_EQUAL);
    for (; qdev != qdev.end(); qdev++) {
        out << "  " << *qdev << " (" << (*qdev)[Property::NUM_SIGNALS_IN] << " inputs)" << std::endl;
    }

    // check graph records
    out << "graph records:" << std::endl;
    for (const Device d : graph.devices()) {
        out << "  device: " << d << std::endl;
        for (Signal s : d.signals(Direction::INCOMING)) {
            out << "    input: " << s << std::endl;
        }
        for (Signal s : d.signals(Direction::OUTGOING)) {
            out << "    output: " << s << std::endl;
        }
    }
    for (Map m : graph.maps()) {
        out << "  map: " << m << std::endl;
    }

    // test API for signal instances
    out << "testing Signal::Instance API" << std::endl;

    int num_inst = 10;
    mapper::Signal multisend = dev.add_signal(Direction::OUTGOING, "multisend", 1, Type::FLOAT,
                                              0, 0, 0, &num_inst);
    mapper::Signal multirecv = dev.add_signal(Direction::INCOMING, "multirecv", 1, Type::FLOAT,
                                              0, 0, 0, &num_inst)
                                  .reserve_instance()
                                  .set_callback(instance_handler, Signal::Event::ALL);
    mapper::Map map2(multisend, multirecv);
    map2.push();
    while (!map2.ready() && !done) {
        dev.poll(10);
    }

    unsigned long id;
    for (int i = 0; i < 200 && !done; i++) {
        id = (rand() % 10) + 5;
        switch (rand() % 5) {
            case 0:
                // try to destroy an instance
                if (verbose)
                    std::cout << "    Retiring " << multisend[Property::NAME] << "." << id
                              << " \t-->  |" << std::endl;
                multisend.instance(id).release();
                break;
            default:
                // try to update an instance
                float v = (rand() % 10) * 1.0f;
                if (verbose)
                    std::cout << "    Updating " << multisend[Property::NAME] << "." << id
                              << " to " << v << " \t-->  |" << std::endl;
                multisend.instance(id).set_value(v);
                break;
        }
        dev.poll(period);
    }

    out << "testing List<Signal::Instance> API" << std::endl;
    multirecv.remove_callback();

    for (int i = 0; i < 200 && !done; i++) {
        id = (rand() % 10) + 5;
        switch (rand() % 5) {
            case 0:
                // try to destroy an instance
                if (verbose)
                    std::cout << "    Retiring " << multisend[Property::NAME] << "." << id
                              << " \t-->  |" << std::endl;
                multisend.instance(id).release();
                break;
            default:
                // try to update an instance
                float v = (rand() % 10) * 1.0f;
                if (verbose)
                    std::cout << "    Updating " << multisend[Property::NAME] << "." << id
                              << " to " << v << " \t-->  |" << std::endl;
                multisend.instance(id).set_value(v);
                break;
        }

        dev.poll(period);

        List<Signal::Instance> instances = multirecv.instances(Object::Status::UPDATE_REM);
        for (; instances != instances.end(); ++instances) {
            Signal::Instance i = *instances;
            if (verbose)
                std::cout << "\t\t\t\t     | --> " << multirecv[Property::NAME] << "." << i.id()
                          << " got " << *(float*)(i.value()) << std::endl;
            ++received;
        }
        instances = multirecv.instances(Object::Status::REL_UPSTRM);
        for (; instances != instances.end(); ++instances) {
            Signal::Instance i = *instances;
            if (verbose)
                std::cout << "\t\t\t\t     | --> " << multirecv[Property::NAME] << "." << i.id()
                          << " got release" << std::endl;
            i.release();
        }
    }

    // test some time manipulation
    Time t1(10, 200);
    Time t2(10, 300);
    if (t1 < t2)
        out << t1 << " is less than " << t2 << std::endl;
    else {
        out << "error:" << t1 << " is not less than " << t2 << std::endl;
        result = 1;
    }
    Time t3 = t1 + t2;
    if (t3 >= t2)
        out << t3 << " is greater than or equal to " << t2 << std::endl;
    else
        out << "error: " << t3 << " is not greater than or equal to " << t2 << std::endl;

    printf("\r..................................................Test %s\x1B[0m.\n",
           result ? "\x1B[31mFAILED" : "\x1B[32mPASSED");
    return result;
}
