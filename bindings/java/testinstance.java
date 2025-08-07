
import mapper.*;
import mapper.graph.*;
import mapper.signal.*;
import mapper.map.*;
import java.util.Arrays;
import java.util.Iterator;

class testinstance {
    public static void main(String [] args) {
        final Device dev1 = new Device("java_testinstance");
        final Device dev2 = new Device("java_testinstance");
        Time start = new Time();
        System.out.println("Current time: "+start.now());

        // This is how to ensure the device is freed when the program
        // exits, even on SIGINT.  The Device must be declared "final".
        Runtime.getRuntime().addShutdownHook(new Thread() {
                @Override
                public void run() {
                        dev1.free();
                        dev2.free();
                    }
            });

        Signal inp1 = dev1.addSignal(mapper.signal.Direction.INCOMING, "insig1", 1, Type.INT32, "Hz",
                                     2.0f, null, 10);
        inp1.addListener(new mapper.object.Listener() {
            public void onEvent(mapper.Object.Instance inst, mapper.object.Status event, int value, Time time) {
                System.out.println(event + " for " + inst.properties().get("name") + ": "
                                   + value + " at t=" + time);
            }});

        System.out.println("Input signal name: "+inp1.properties().get("name"));

        Signal out1 = dev2.addSignal(mapper.signal.Direction.OUTGOING, "outsig1", 1, Type.INT32,
                                     "Hz", 0, 1, 10);
        Signal out2 = dev2.addSignal(mapper.signal.Direction.OUTGOING, "outsig2", 2, Type.FLOAT,
                                     "Hz", 0.0f, 1.0f, 10);

        System.out.print("Waiting for ready... ");
        while (!dev1.ready() || !dev2.ready()) {
            dev1.poll(50);
            dev2.poll(50);
        }
        System.out.println("OK");

        mapper.Map map = new mapper.Map(out1, inp1);
        map.properties().put(Property.EXPRESSION, "y=x*100");
        map.push();

        System.out.print("Establishing map... ");
        while (!map.ready()) {
            dev1.poll(50);
            dev2.poll(50);
        }
        System.out.println("OK");

        int i = 0;

        // Signal should report no value before the first update.
        if (out1.hasValue())
            System.out.println("Signal has value: " + out1.getValue());
        else
            System.out.println("Signal has no value.");

        // Test instances
        out1.addListener(new mapper.object.Listener() {
            public void onEvent(mapper.Object.Instance inst, mapper.object.Status event, int value, Time time) {
                System.out.println(event + " for " + inst.properties().get("name")
                                   + " instance " + inst.id() + ": " + event);
                java.lang.Object userObject = inst.getUserReference();
                if (userObject != null) {
                    System.out.println("userObject.class = "+userObject.getClass());
                    if (userObject.getClass().equals(int[].class)) {
                        System.out.println("  got int[] userObject "
                                           + Arrays.toString((int[])userObject));
                    }
                }
            }}, mapper.object.Status.ANY);

        inp1.addListener(new mapper.object.Listener() {
            public void onEvent(mapper.Object.Instance inst, mapper.object.Status event, int val, Time time) {
                System.out.println(event + " for " + inst.properties().get("name")
                                   + " instance " + inst.id() + ": " + inst.getUserReference()
                                   + ", val= " + val);
            }});

        System.out.println(inp1.properties().get("name") + " stealing mode: "
                           + inp1.properties().get(Property.STEALING));
        inp1.properties().put(Property.STEALING, Stealing.NEWEST);
        System.out.println(inp1.properties().get("name") + " stealing mode: "
                           + inp1.properties().get(Property.STEALING));

        System.out.println("Reserving 4 instances for signal " + out1.properties().get("name"));
        out1.reserveInstances(4);
        // int[] foo = new int[]{1,2,3,4};
        // Signal.Instance instance1 = out1.instance(foo);
        Signal.Instance instance1 = out1.instance();
        Signal.Instance instance2 = out1.instance();

        System.out.println("Reserving 4 instances for signal " + inp1.properties().get("name"));
        inp1.reserveInstances(4);

        while (i++ <= 100) {
            if ((i % 3) > 0) {
                instance1.setValue(i);
                System.out.println("Updated instance1 value to: " + instance1.getValue());
            }
            else {
                instance2.setValue(i);
                System.out.println("Updated instance2 value to: " + instance2.getValue());
            }

            dev1.poll(50);
            dev2.poll(50);
        }

        System.out.println(inp1.properties().get("name") + " oldest instance is "
                           + inp1.oldestActiveInstance());
        System.out.println(inp1.properties().get("name") + " newest instance is "
                           + inp1.newestActiveInstance());

        System.out.println("Repeating update loop without callbacks");
        inp1.removeListener();

        i = 0;
        while (i++ <= 100) {
            if ((i % 3) > 0) {
                instance1.setValue(i);
                System.out.println("Updated instance1 value to: " + instance1.getValue());
            }
            else {
                instance2.setValue(i);
                System.out.println("Updated instance2 value to: " + instance2.getValue());
            }

            dev1.poll(50);
            dev2.poll(50);

            List<Signal.Instance> updated = inp1.instances(mapper.object.Status.REMOTE_UPDATE);
            for (Signal.Instance inst : updated) {
                System.out.println(inst.properties().get("name")
                                 + " instance " + inst.id() + " got value " + inst.getValue());
            }
        }
    }
}
