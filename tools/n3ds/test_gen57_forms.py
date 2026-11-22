"""Unit tests of the forms builder's pure helpers (made-up names, no dump):  python -I tools/n3ds/test_gen57_forms.py"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.modules.setdefault('gen57_data', type(sys)('gen57_data'))
import gen57_forms as F

STONES = {656: 'Gengarite', 660: 'Charizardite X', 678: 'Charizardite Y', 661: 'Blastoisinite', 684: 'Latiasite', 685: 'Latiosite',
          663: 'Mewtwonite Y', 662: 'Mewtwonite X', 677: 'Absolite'}
fails = 0


def check(ok, what):
    global fails
    print(('ok   ' if ok else 'FAIL ') + what)
    fails += 0 if ok else 1


check(F.form_key('Raichu', 'alola', 1, 26) == 'Raichu (Alola)', 'Alolan key')
check(F.form_key('Charizard', 'mega', 2, 6) == 'Charizard (Mega Y)', 'Mega X / Y key')
check(F.form_key('Gengar', 'mega', 1, 94) == 'Gengar (Mega)', 'plain Mega key')
check(F.shown_form('Gengar', 'mega', 94, 1) == 'MEGA GENGAR', 'Mega name')
check(F.stone_for(None, STONES, 'Gengar', 1, 94)[1] == 'Gengarite', 'Gengarite')
check(F.stone_for(None, STONES, 'Charizard', 1, 6)[1] == 'Charizardite X', 'Charizard X stone')
check(F.stone_for(None, STONES, 'Charizard', 2, 6)[1] == 'Charizardite Y', 'Charizard Y stone')
check(F.stone_for(None, STONES, 'Mewtwo', 2, 150)[1] == 'Mewtwonite Y', 'Mewtwo Y stone')
check(F.stone_for(None, STONES, 'Blastoise', 1, 9)[1] == 'Blastoisinite', 'Blastoisinite')
check(F.stone_for(None, STONES, 'Latias', 1, 380)[1] == 'Latiasite', 'Latiasite, not Latiosite')
check(F.stone_for(None, STONES, 'Latios', 1, 381)[1] == 'Latiosite', 'Latiosite')
check(F.stone_for(None, STONES, 'Absol', 1, 359)[1] == 'Absolite', 'Absolite')
check(F.stone_for(None, STONES, 'Pikachu', 1, 25)[1] is None, 'no stone for Pikachu')
sys.exit(1 if fails else 0)
