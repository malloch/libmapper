using System.Collections;
using System.Runtime.InteropServices;

namespace Mapper;

public abstract class _List
{
    protected IntPtr _list;
    private bool _started;
    protected int _status;
    protected int _index;
    protected Mapper.Type _type;

    public _List()
    {
        _type = Mapper.Type.Null;
        _list = IntPtr.Zero;
        _started = false;
    }

    public _List(IntPtr list, Mapper.Type type)
    {
        _type = type;
        _list = list;
        _started = false;
    }

    public _List(IntPtr sig, int status, int num)
    {
        _type = Mapper.Type.Null;
        _list = sig;
        _started = false;
        _status = status;
        _index = num;
    }

    /* copy constructor */
    public _List(_List original)
    {
        _list = mpr_list_get_cpy(original._list);
        _type = original._type;
        _started = false;
    }

    public int Count => mpr_list_get_size(_list);

    // TODO: probably need refcounting to check if we should free the underlying mpr_list
    [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
    private static extern void mpr_list_free(IntPtr list);

    public void Free()
    {
        mpr_list_free(_list);
        _list = IntPtr.Zero;
    }

    [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
    private static extern int mpr_list_get_size(IntPtr list);

    [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
    private static extern IntPtr mpr_list_get_idx(IntPtr list, int index);

    public IntPtr GetIdx(int index)
    {
        return mpr_list_get_idx(_list, index);
    }

    public IntPtr Deref()
    {
        unsafe
        {
            return new IntPtr(_list != IntPtr.Zero ? *(void**)_list : null);
        }
    }

    [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
    private static extern IntPtr mpr_list_get_cpy(IntPtr list);

    [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
    private static extern IntPtr mpr_list_get_union(IntPtr list1, IntPtr list2);

    public _List Join(_List rhs)
    {
        _list = mpr_list_get_union(_list, mpr_list_get_cpy(rhs._list));
        return this;
    }

    public static IntPtr Union(_List a, _List b)
    {
        return mpr_list_get_union(mpr_list_get_cpy(a._list), mpr_list_get_cpy(b._list));
    }

    [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
    private static extern IntPtr mpr_list_get_isect(IntPtr list1, IntPtr list2);

    public _List Intersect(_List rhs)
    {
        _list = mpr_list_get_isect(_list, mpr_list_get_cpy(rhs._list));
        return this;
    }

    public static IntPtr Intersection(_List a, _List b)
    {
        return mpr_list_get_isect(mpr_list_get_cpy(a._list), mpr_list_get_cpy(b._list));
    }

    [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
    private static extern IntPtr mpr_list_get_diff(IntPtr list1, IntPtr list2);

    public _List Subtract(_List rhs)
    {
        _list = mpr_list_get_diff(_list, mpr_list_get_cpy(rhs._list));
        return this;
    }

    public static IntPtr Difference(_List a, _List b)
    {
        return mpr_list_get_diff(mpr_list_get_cpy(a._list), mpr_list_get_cpy(b._list));
    }

    [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
    private static extern IntPtr mpr_list_get_next(IntPtr list);

    public bool GetNext()
    {
        if (0 == _status) {
            if (_started)
                _list = mpr_list_get_next(_list);
            else
                _started = true;
        }
        else {
            if (--_index < 0)
                _list = IntPtr.Zero;
        }
        return _list != IntPtr.Zero;
    }

    public override string ToString()
    {
        return $"Mapper.List<{_type}>";
    }
}

public class List<T> : _List, IEnumerator, IEnumerable, IDisposable
    where T : Mapper.Object, new()
{
    internal List(IntPtr list, Mapper.Type type) : base(list, type)
    {
    }

    internal List(IntPtr sig, int status, int num) : base(sig, status, num)
    {
    }

    public T this[int index]
    {
        get
        {
            var t = new T();
            if (typeof(T) == typeof(Mapper.Signal.Instance)) {
                t.NativePtr = _list;
                t.SetInstId(_status, index);
            }
            else {
                t.NativePtr = GetIdx(index);
            }
            return t;
        }
    }

    public T Current
    {
        get
        {
            var t = new T();
            if (typeof(T) == typeof(Mapper.Signal.Instance)) {
                t.NativePtr = _list;
                t.SetInstId(_status, _index);
            }
            else {
                t.NativePtr = Deref();
            }
            return t;
        }
    }

    void IDisposable.Dispose()
    {
        Free();
    }

    /* Methods for enumeration */
    public IEnumerator GetEnumerator()
    {
        return this;
    }

    public void Reset()
    {
        throw new NotSupportedException();
    }

    public bool MoveNext()
    {
        return GetNext();
    }

    object IEnumerator.Current => Current;

    /* Overload some arithmetic operators */
    public static Mapper.List<T> operator +(Mapper.List<T> a, Mapper.List<T> b)
    {
        if (typeof(T) == typeof(Mapper.Signal.Instance))
            throw new NotSupportedException();
        return new Mapper.List<T>(Union(a, b), a._type);
    }

    public static Mapper.List<T> operator *(Mapper.List<T> a, Mapper.List<T> b)
    {
        if (typeof(T) == typeof(Mapper.Signal.Instance))
            throw new NotSupportedException();
        return new Mapper.List<T>(Intersection(a, b), a._type);
    }

    public static Mapper.List<T> operator -(Mapper.List<T> a, Mapper.List<T> b)
    {
        if (typeof(T) == typeof(Mapper.Signal.Instance))
            throw new NotSupportedException();
        return new Mapper.List<T>(Difference(a, b), a._type);
    }
}