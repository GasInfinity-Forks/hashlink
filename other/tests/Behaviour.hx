class Behaviour {
    static function testStaticFunctionCall(): Void trace("Static function call: YES");
    static function testStaticFunctionReturn(): Int return 42; // True random number
    static function testStaticFunctionArgsIntegerAdd(a: Int, b: Int): Int return a + b;

    static function getTrue(): Bool return true;

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
        trace('Int null assignment: YES');

        intNull = 69;
        trace('Int null reassignment: $intNull');
    }
}