class Behaviour {
    static function testStaticFunctionCall(): Void trace("Static function call: YES");
    static function testStaticFunctionReturn(): Int return 42; // True random number
    static function testStaticFunctionArgsIntegerAdd(a: Int, b: Int): Int return a + b;

    static function getTrue(): Bool return true;
    static function getFalse(): Bool return false;

    static function testIntegerComparisons(a: Int, b: Int): Void {
        var eq = a == b;
        var notEq = a != b;

        var gt = a > b;
        var ge = a >= b;
        var lt = a < b;
        var le = a <= b;
        
        trace("Integer comparisons: ");
        trace('($a, $b) == $eq');
        trace('($a, $b) != $notEq');
        trace('($a, $b) == $gt');
        trace('($a, $b) == $ge');
        trace('($a, $b) == $lt');
        trace('($a, $b) == $le');
    }

    static function testIntegerBitwise(a: Int, b: Int): Void {
        var shl = a << b;
        var shr = a >> b;
        var ushr = a >>> b;

        trace('Integer bitwise ($a, $b) L[$shl] R[$shr] UR[$ushr]');
    }

    static function main(): Void {
        trace("Trace: YES");
        
        testStaticFunctionCall();

        var testFunctionReturn = testStaticFunctionReturn();
        trace('Function return: $testFunctionReturn');

        var testArgsIntAdd = testStaticFunctionArgsIntegerAdd(20, 20);
        trace('Integer addition: 40 == $testArgsIntAdd?');

        var testIsTrueCompare = getTrue() == true;
        var testIsFalseCompare = getTrue() == false;
        trace('Boolean comparison: T[$testIsTrueCompare] F[$testIsFalseCompare]');

        var dynNull: Dynamic = null;
        trace('Dynamic null asignment: YES');

        var intNull: Null<Int> = null;
        trace('Integer null assignment: YES');

        intNull = 69;
        trace('Integer null reassignment: $intNull');

        testIntegerComparisons(20, 40);
        testIntegerBitwise(2, 1);
    }
}