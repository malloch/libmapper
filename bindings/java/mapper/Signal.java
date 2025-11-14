
package mapper;

import mapper.Map;
import mapper.signal.*;
import mapper.Device;
import mapper.object.*;
import java.util.EnumSet;
import java.lang.reflect.Method;
import java.lang.reflect.Modifier;

public class Signal extends mapper.Object
{
    /* constructors */
    public Signal(long sig) {
        super(sig);
    }

    /* self */
    @Override
    public Signal self() {
        return this;
    }

    /* device */
    public native Device device();

    /* callbacks */
    private native void mapperSignalSetCB(long sig, Listener l, String methodSig, int flags);
    private void _setListener(Listener l, int flags) {
        if (l == null) {
            mapperSignalSetCB(_obj, null, null, 0);
            return;
        }

        // try to check type of listener
        String[] instanceName = l.toString().split("@", 0);
        for (Method method : l.getClass().getMethods()) {
            if ((method.getModifiers() & Modifier.STATIC) != 0)
                continue;
            String methodName = method.toString();
            if (methodName.startsWith("public void "+instanceName[0]+".")) {
                mapperSignalSetCB(_obj, l, methodName, flags);
                return;
            }
        }
        System.out.println("Error: no match for listener.");
    }
    public Signal setListener(Listener l, Event event) {
        _setListener(l, event.value());
        return this;
    }
    public Signal setListener(Listener l, EnumSet<Event> events) {
        int flags = 0;
        for (Event e : Event.values()) {
            if (events.contains(e))
                flags |= e.value();
        }
        _setListener(l, flags);
        return this;
    }
    public Signal setListener(Listener l) {
        _setListener(l, Status.REMOTE_UPDATE.value());
        return this;
    }

    private native void mapperSignalReserveInstances(long sig, int num, long[] ids);
    public Signal reserveInstances(int num) {
        mapperSignalReserveInstances(_obj, num, null);
        return this;
    }
    public Signal reserveInstances(long[] ids) {
        mapperSignalReserveInstances(_obj, 0, ids);
        return this;
    }

    public Instance instance(long id) {
        return new Instance(id);
    }
    public Instance instance() {
        return new Instance();
    }
    public Instance instance(java.lang.Object obj) {
        return new Instance(obj);
    }

    /* instance management */
    public native Instance oldestActiveInstance();
    public native Instance newestActiveInstance();

    private native int _num_instances(long sig, int status);
    public int numInstances(Status status)
    {
        return _num_instances(_obj, status.value());
    }
    public int numInstances(EnumSet<Status> statuses)
    {
        int flags = 0;
        for (Status is : Status.values()) {
            if (statuses.contains(is))
                flags |= is.value();
        }
        return _num_instances(_obj, flags);
    }

    /* set value */
    public native Signal setValue(long id, java.lang.Object value);
    public Signal setValue(java.lang.Object value) {
        return setValue(0, value);
    }

    /* get value */
    public native boolean hasValue(long id);
    public boolean hasValue() { return hasValue(0); }

    public native java.lang.Object getValue(long id);
    public java.lang.Object getValue() { return getValue(0); }

    public native Signal releaseInstance(long id);
    public native Signal removeInstance(long id);

    public class Instance extends mapper.Object
    {
        /* constructors */
        private native long mapperInstance(boolean hasId, long id, java.lang.Object obj);
        private Instance(long id) {
            super(Signal.this._obj);
            _id = mapperInstance(true, id, null);
        }
        private Instance(java.lang.Object obj) {
            super(Signal.this._obj);
            _id = mapperInstance(false, 0, obj);
        }
        private Instance() {
            super(Signal.this._obj);
            _id = mapperInstance(false, 0, null);
        }

        /* self */
        @Override
        public Instance self() {
            return this;
        }

        /* release */
        public void release() { Signal.this.releaseInstance(_id); }

        /* remove instances */
        public void free() { Signal.this.removeInstance(_id); }

        public Signal signal() { return Signal.this; }

        public long id() { return _id; }

        public boolean hasValue() { return Signal.this.hasValue(_id); }

        /* update */
        public Instance setValue(java.lang.Object value) {
            Signal.this.setValue(_id, value);
            return this;
        }

        /* value */
        public java.lang.Object getValue() { return Signal.this.getValue(_id); }

        /* userObject */
        public native java.lang.Object getUserReference();
        public native Instance setUserReference(java.lang.Object obj);

        /* properties */
        // TODO: filter for instance-specific properties like id, value, time

        private native int get_status(long sig, boolean clear_volatile);
        public EnumSet<Status> getStatus(boolean clear_volatile)
        {
            EnumSet<Status> jstatus = EnumSet.noneOf(Status.class);
            int cstatus = get_status(_obj, clear_volatile);
            for (Status s : Status.values()) {
                if ((cstatus & s.value()) != 0)
                    jstatus.add(s);
            }
            return jstatus;
        }
        public EnumSet<Status> getStatus()
        {
            return getStatus(false);
        }

        private long _id;
    }

    /* retrieve associated maps */
    private native long maps(long sig, int dir);
    public mapper.List<mapper.Map> maps(Direction dir)
        { return new mapper.List<mapper.Map>(maps(_obj, dir.value())); }
    public mapper.List<mapper.Map> maps()
        { return maps(Direction.ANY); }
}
