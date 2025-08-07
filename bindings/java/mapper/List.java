
package mapper;

import java.util.Iterator;
import java.util.Collection;

public class List<T extends mapper.Object> implements Iterable<T> {

    /* constructor */
    public List(long listptr) {
        _ptr = listptr;
        _status = 0;
    }

    public List(java.lang.Object sigobj, long sigptr, int status, int num) {
        _sigobj = sigobj;
        _ptr = sigptr;
        _status = status;
        _idx = num - 1;
    }

    private native T _get(long ptr, java.lang.Object sigobj, int status, int index);
    public T get(int index) {
        return _get(_ptr, _sigobj, _status, index);
    }

    private native int _size(long ptr, int status);
    public int size() {
        return _size(_ptr, _status);
    }

    public boolean isEmpty() {
        return _ptr == 0;
    }

    private native boolean _contains(long ptr, T t);
    public boolean contains(T t) {
        return _contains(_ptr, t);
    }

    private native boolean _containsAll(long ptr_1, long ptr_2);
    public boolean containsAll(List<T> l) {
        return _containsAll(_ptr, l._ptr);
    }

    private native long _union(long lhs, long rhs);
    public List join(List<T> l) {
        if (0 == _status)
            _ptr = _union(_ptr, l._ptr);
        return this;
    }

    private native long _diff(long lhs, long rhs);
    public List difference(List<T> l) {
        if (0 == _status)
            _ptr = _diff(_ptr, l._ptr);
        return this;
    }

    private native long _isect(long lhs, long rhs);
    public List intersect(List<T> l) {
        if (0 == _status)
            _ptr = _isect(_ptr, l._ptr);
        return this;
    }

    private native T[] _toArray(long ptr, int status);
    public T[] toArray()
        { return _toArray(_ptr, _status); }

    private native long _copy(long ptr);
    private native long _deref(long ptr);
    private native long _next(long ptr);
    public Iterator<T> iterator() {
        Iterator<T> it = new Iterator<T>() {
            private long _listcopy = (0 == _status) ? _copy(_ptr) : _ptr;

//            protected void finalize() {
//                if (_listcopy != 0)
//                    mapperListFree(_listcopy);
//            }

            @Override
            public boolean hasNext() {
                if (0 == _status)
                    return _listcopy != 0;
                else
                    return _idx > 0;
            }

            @Override
            public T next() {
                if (0 == _listcopy)
                    return null;

                if (0 == _status) {
                    long temp = _deref(_listcopy);
                    _listcopy = _next(_listcopy);
                    return _get(temp, 0, 0, 0);
                }
                else {
                    --_idx;
                    if (_idx < 0) {
                        _listcopy = 0;
                        return null;
                    }
                    return _get(_ptr, _sigobj, _status, _idx);
                }
            }

            @Override
            public void remove() {
                throw new UnsupportedOperationException();
            }
        };
        return it;
    }

    private native void _filter(long list, int id, String key, java.lang.Object value, int op);
    public List filter(String key, java.lang.Object value, Operator operator) {
        if (0 == _status)
            _filter(_ptr, 0, key, value, operator.value());
        return this;
    }
    public List filter(Property id, java.lang.Object value, Operator operator) {
        if (0 == _status)
            _filter(_ptr, id.value(), null, value, operator.value());
        return this;
    }
    //TODO: add version passing filter function

//    private native void _free(long list);
//    protected void finalize() {
//        if (_ptr != 0)
//            _free(_ptr);
//    }

    private long _ptr;
    private int _status;
    private int _idx;
    private java.lang.Object _sigobj;
}
