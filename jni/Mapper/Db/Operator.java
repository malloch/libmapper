
package mapper.db;

/*! Possible operations for composing db queries. */
public enum Operator {
    DOES_NOT_EXIST          (0),
    EQUAL                   (1),
    EXISTS                  (2),
    GREATER_THAN            (3),
    GREATER_THAN_OR_EQUAL   (4),
    LESS_THAN               (5),
    LESS_THAN_OR_EQUAL      (6),
    NOT_EQUAL               (7);

    Operator(int value) {
        this._value = value;
    }

    public int value() {
        return _value;
    }

    private int _value;
}