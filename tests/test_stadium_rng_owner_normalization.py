"""Baseline/candidate equivalence; modeled owners are not runtime evidence."""
import importlib.util
import os
from pathlib import Path
import unittest
from unittest.mock import patch

CANDIDATE = Path(__file__).resolve().parents[1]
SOURCE = Path(os.environ.get('MELEE_NORMALIZATION_SOURCE_ROOT', CANDIDATE))


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


baseline = load('normalization_baseline', SOURCE/'tools/compare_stadium_long_b_rng.py')
candidate = load('normalization_candidate', CANDIDATE/'tools/compare_stadium_long_b_rng.py')
owners = load('normalization_owners', SOURCE/'tools/compare_rng_owner_window.py')


def owner(side, role):
    original = side == 'original'
    r = dict(status='scheduled_proc', callback_symbol='grStadium_801D1390',
             classifier=3,p_link=4,s_link=1,
             gobj='0x80001000' if original else '0000000000001000',
             proc='0x80002000' if original else '0000000000002000',
             callback='0x801d1390' if original else '0000000000000080')
    if role == 'particle':r.update(callback_symbol='efLib_particles_proc_main',classifier=8,p_link=11,s_link=15)
    if role == 'outside':
        r.update(status='outside_proc',callback_symbol=None,classifier=0,p_link=0,s_link=0)
        for k in ['gobj','proc','callback']:r[k]='0x00000000' if original else '0000000000000000'
    if original:r.update(source_slot=None,entity=None,fighter_kind=None)
    else:r['fighter']=None
    if role == 'fighter':
        r.update(callback_symbol='Fighter_8006A360',classifier=4,p_link=8,s_link=1)
        if original:r['callback']='0x8006a360'
        if original:r.update(source_slot=1,entity=0,fighter_kind=0)
        else:r['fighter']=dict(slot=1,entity=0,kind=0)
    return r


class OwnerNormalizationTests(unittest.TestCase):
    def both(self, value, side, valid):
        if valid:
            actual=candidate._owner_semantics(value,side,'control',owners)
            if SOURCE.resolve()!=CANDIDATE.resolve():
                self.assertEqual(baseline._owner_semantics(value,side,'control',owners),actual)
            semantic,pointers=actual
            self.assertEqual(pointers,{k:value[k] for k in ['gobj','proc','callback']})
            self.assertEqual(semantic['callback_symbol'],value['callback_symbol'])
        else:
            for module in [baseline,candidate]:
                with self.assertRaises(module.EvidenceError):
                    module._owner_semantics(value,side,'control',owners)

    def test_all_roles_keep_identical_valid_results_and_use_shared_semantic_leaf(self):
        for side in ['original','browser']:
            for role in ['stage','particle','outside','fighter']:
                self.both(owner(side,role),side,True)
            with patch.object(owners,'_original_owner',side_effect=AssertionError('duplicate pointer validator')), \
                 patch.object(owners,'_native_owner',side_effect=AssertionError('duplicate pointer validator')), \
                 patch.object(owners,'_semantic_owner',wraps=owners._semantic_owner) as checked:
                candidate._owner_semantics(owner(side,'particle'),side,'control',owners)
                self.assertEqual(checked.call_count,1)

    def test_each_pointer_field_still_rejects_shape_range_zero_and_alignment_mutations(self):
        for side in ['original','browser']:
            for field in ['gobj','proc','callback']:
                values=[None,'wrong','0x00000001','0000000000000000']
                if side=='original':values+=['0x00000000','0x80000001','0x81800000']
                elif field!='callback':values+=['0000000000000001']
                for value in values:
                    r=owner(side,'stage');r[field]=value
                    with self.subTest(side=side,field=field,value=value):self.both(r,side,False)
            r=owner(side,'outside');r['proc']='0x80001000' if side=='original' else '0000000000001000'
            self.both(r,side,False)

    def test_semantic_ranges_symbols_particle_tuple_and_nonfighter_identity_still_refuse(self):
        for side in ['original','browser']:
            for field,values in [('status',['unknown',None]),('classifier',[True,-1,65536]),
                                 ('p_link',[True,-1,256]),('s_link',[True,-1,256]),
                                 ('callback_symbol',['unknown',None])]:
                for value in values:
                    r=owner(side,'stage');r[field]=value;self.both(r,side,False)
            r=owner(side,'particle');r['s_link']=14;self.both(r,side,False)
            if side=='original':
                for field in ['source_slot','entity','fighter_kind']:
                    r=owner(side,'stage');r[field]=0;self.both(r,side,False)
            else:
                r=owner(side,'stage');r['fighter']={};self.both(r,side,False)
                del r['fighter'];self.both(r,side,False)

    def test_fighter_branch_keeps_exact_role_and_slot_entity_kind_validation(self):
        for side in ['original','browser']:
            for field in ['classifier','p_link','s_link']:
                r=owner(side,'fighter');r[field]+=1;self.both(r,side,False)
            for field,value in [('slot',4),('slot',True),('entity',2),('kind',-1),('kind',2**32)]:
                r=owner(side,'fighter')
                if side=='original':r[{'slot':'source_slot','entity':'entity','kind':'fighter_kind'}[field]]=value
                else:r['fighter'][field]=value
                self.both(r,side,False)


if __name__=='__main__':unittest.main()
