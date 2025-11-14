
package mapper;

import mapper.NativeLib;
import mapper.object.*;
import mapper.Property;
import java.util.EnumSet;

public abstract class Object<T extends mapper.Object<T>>
{
    /* constructor */
    public Object(long obj) { _obj = obj; _owned = false; }

    /* self */
    abstract T self();

    /* graph */
    private native long graph(long obj);
    public mapper.Graph graph() {
        return new mapper.Graph(graph(_obj));
    }

    /* status */
    private native int get_status(long sig, boolean clear_volatile);
    public EnumSet<Status> getStatus(boolean clear_volatile)
    {
        EnumSet<Status> jstatus = EnumSet.noneOf(Status.class);
        int cstatus = get_status(_obj, clear_volatile);
        for (Status s : Status.values()) {
            if (Status.ANY == s)
                continue;
            if ((cstatus & s.value()) != 0)
                jstatus.add(s);
        }
        return jstatus;
    }
    public EnumSet<Status> getStatus()
    {
        return getStatus(false);
    }

    /* properties */
    public class Properties
    {
        /* constructor */
        public Properties(Object obj) { _obj = obj._obj; }

        public void clear()
            { throw new UnsupportedOperationException(); }

        private native boolean _containsKey(long obj, int idx, String key);
        public boolean containsKey(java.lang.Object key)
        {
            if (key instanceof Integer)
                return _containsKey(_obj, (int)key, null);
            else if (key instanceof String)
                return _containsKey(_obj, 0, (String)key);
            else if (key instanceof mapper.Property)
                return _containsKey(_obj, ((mapper.Property)key).value(), null);
            else
                return false;
        }

        private native boolean _containsValue(long obj, java.lang.Object value);
        public boolean containsValue(java.lang.Object value)
            { return _containsValue(_obj, value); }

        public class Entry
        {
            private native Entry _set(long obj, int idx, String key);

            public String getKey()
                { return _key; }
            public mapper.Property getProperty()
                { return mapper.Property.values()[_id]; }
            public java.lang.Object getValue()
                { return _value; }
            public String toString()
                { return _key + ": " + _value.toString(); }

            private int _id;
            private String _key;
            private java.lang.Object _value;
        }

        // TODO: implement iterator

        public Entry getEntry(java.lang.Object key)
        {
            if (key instanceof Integer) {
                Entry e = new Entry();
                return e._set(_obj, (int)key, null);
            }
            else if (key instanceof String) {
                Entry e = new Entry();
                return e._set(_obj, 0, (String)key);
            }
            else if (key instanceof mapper.Property) {
                Entry e = new Entry();
                return e._set(_obj, ((mapper.Property)key).value(), null);
            }
            else
                return null;
        }

        private native java.lang.Object _get(long obj, int idx, String key);
        public java.lang.Object get(java.lang.Object key)
        {
            if (key instanceof Integer)
                return _get(_obj, (int)key, null);
            else if (key instanceof String)
                return _get(_obj, 0, (String)key);
            else if (key instanceof mapper.Property)
                return _get(_obj, ((mapper.Property)key).value(), null);
            else
                return null;
        }

        public boolean isEmpty()
            { return false; }

        private native java.lang.Object _put(long obj, int idx, String key, java.lang.Object value,
                                             boolean publish);
        public java.lang.Object put(java.lang.Object key, java.lang.Object value, boolean publish)
        {
            // translate some enum objects to their int form
            if (value instanceof mapper.signal.Direction)
                value = ((mapper.signal.Direction)value).value();
            else if (value instanceof mapper.map.Location)
                value = ((mapper.map.Location)value).value();
            else if (value instanceof mapper.map.Protocol)
                value = ((mapper.map.Protocol)value).value();
            else if (value instanceof mapper.signal.Stealing)
                value = ((mapper.signal.Stealing)value).value();
            else if (value instanceof mapper.Type)
                value = ((mapper.Type)value).value();

            if (key instanceof Integer)
                return _put(_obj, (int)key, null, value, publish);
            else if (key instanceof String)
                return _put(_obj, 0, (String)key, value, publish);
            else if (key instanceof mapper.Property)
                return _put(_obj, ((mapper.Property)key).value(), null, value, publish);
            else
                return null;
        }

        public java.lang.Object put(java.lang.Object key, java.lang.Object value)
            { return put(key, value, true); }

        private native java.lang.Object _remove(long obj, int idx, String key);
        public java.lang.Object remove(java.lang.Object key)
        {
            if (key instanceof Integer)
                return _remove(_obj, (int)key, null);
            else if (key instanceof String)
                return _remove(_obj, 0, (String)key);
            else if (key instanceof mapper.Property)
                return _remove(_obj, ((mapper.Property)key).value(), null);
            else
                return null;
        }

        private native int _size(long obj);
        public int size()
            { return _size(_obj); }
    }

    public Properties properties()
        { return new Properties(this); }

    private native void _push(long obj);
    public T push() {
        _push(_obj);
        return self();
    }

    // Note: this is _not_ guaranteed to run, the user should still
    // call free() explicitly when the object is no longer needed.
//    protected void finalize() throws Throwable {
//        try {
//            free();
//        } finally {
//            super.finalize();
//        }
//    }

    protected long _obj;
    protected boolean _owned;
    public boolean valid() {
        return _obj != 0;
    }

    static {
        System.loadLibrary(NativeLib.name);
    }
}
