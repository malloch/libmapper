
package mapper.object;

/*! Describes possible statuses for a libmapper object. */
public enum Status {
    UNDEFINED           (0x0000),
    NEW                 (0x0001),
    MODIFIED            (0x0002),
    REMOVED             (0x0004),
    EXPIRED             (0x0008),
    STAGED              (0x0010),
    ACTIVE              (0x0020),
    HAS_VALUE           (0x0040),
    NEW_VALUE           (0x0080),
    LOCAL_UPDATE        (0x0100),
    REMOTE_UPDATE       (0x0200),
    UPSTREAM_RELEASE    (0x0400),
    DOWNSTREAM_RELEASE  (0x0800),
    OVERFLOW            (0x1000),
    ANY                 (0x1FFF);

    Status(int value) {
        this._value = value;
    }

    public int value() {
        return _value;
    }

    private int _value;
}
