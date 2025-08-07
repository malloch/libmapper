
package mapper;

import mapper.object.*;
import mapper.NativeLib;
import mapper.Property;
import java.util.EnumSet;

public class Object
{
    /* constructor */
    public Object(long obj) { _obj = obj; _owned = false; }

    /* self */
    Object self() {
        return this;
    }

    /* graph */
    private native long graph(long obj);
    public mapper.Graph graph() {
        return new mapper.Graph(graph(_obj));
    }

    /* status */
    public native int getStatus();

    private native void _reset_status(long obj);
    public Object resetStatus() {
        _reset_status(_obj);
        return self();
    }

    /* callbacks */
    private native void mapperObjectAddCB(long obj, Listener l, int flags);
    private void _addListener(Listener l, int flags) {
        if (l != null)
            mapperObjectAddCB(_obj, l, flags);
    }
    public Object addListener(Listener l) {
        _addListener(l, Status.REMOTE_UPDATE.value());
        return this;
    }
    public Object addListener(Listener l, Status status) {
        _addListener(l, status.value());
        return this;
    }
    public Object addListener(Listener l, EnumSet<Status> statuses) {
        int flags = 0;
        for (Status s : Status.values()) {
            if (statuses.contains(s))
                flags |= s.value();
        }
        _addListener(l, flags);
        return this;
    }

    private native void _removeListener(long obj, Listener l);
    public Object removeListener(Listener l) {
        _removeListener(_obj, l);
        return this;
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
    public Object push() {
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
