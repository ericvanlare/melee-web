"""Clearly modeled controls for actual comparison functions; no runtime parity."""
import copy
import importlib.util
from pathlib import Path
import unittest

path = Path(__file__).resolve().parents[1] / 'tools/compare_stadium_registration.py'
spec = importlib.util.spec_from_file_location('registration_comparator', path)
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)


def fixture():
    rows, events, edges, bounds = [], [], [], []
    def native(k, w, **extra):
        r = dict(schema='melee-web-source-registration-row-v1', sequence=len(rows), kind=k,
                 world_generation=w, object_generation='0', gobj_generation='0', proc_generation='0',
                 object='0x0', gobj='0x0', proc='0x0', callback='0x0', owner_role=0,
                 classifier=0, p_link=0, s_link=0, pool_kind=0, residual_live=0,
                 call_index=4294967295, call_depth=0)
        r.update(extra); rows.append(r); return r
    for mi in range(2):
        w = str(30 + mi); g, p = str(2*mi+1), str(2*mi+2)
        og, op = str(100+2*mi+1), str(100+2*mi+2)
        # The exact original address is deliberately reused across lifetimes.
        ga, pa, nga, npa = 0x80001000, 0x80002000, 0x1000+mi*0x100, 0x2000+mi*0x100
        base = dict(gobj=hex(nga), proc=hex(npa), callback='0xabc', gobj_generation=g,
                    proc_generation=p, owner_role=1, classifier=3, p_link=4, s_link=1)
        start = len(rows); native(1, w)
        native(3, w, object=hex(nga), object_generation=g, pool_kind=1)
        setup = native(6, w, **{**base, 'proc':'0x0', 'proc_generation':'0'}, call_index=len(rows))
        native(3, w, object=hex(npa), object_generation=p, pool_kind=2)
        link = native(12, w, **base, call_index=len(rows), call_depth=1)
        native(8, w, **base, call_index=link['sequence'], call_depth=1)
        sr = native(7, w, **base, call_index=setup['sequence'])
        remove = native(11, w, **base, call_index=len(rows))
        owner = native(14, w, **base, call_index=len(rows), call_depth=1)
        sched = native(13, w, **base, call_index=len(rows), call_depth=2)
        native(9, w, **base, call_index=sched['sequence'], call_depth=2)
        native(10, w, **base, call_index=owner['sequence'], call_depth=1)
        free = native(4, w, object=hex(npa), object_generation=p, pool_kind=2,
                      call_index=len(rows), call_depth=1)
        native(5, w, object=hex(npa), object_generation=p, pool_kind=2,
               call_index=free['sequence'], call_depth=1)
        native(15, w, **base, call_index=remove['sequence'])
        free = native(4, w, object=hex(nga), object_generation=g, pool_kind=1, call_index=len(rows))
        native(5, w, object=hex(nga), object_generation=g, pool_kind=1, call_index=free['sequence'])
        end = len(rows); native(2, w)
        bounds.append(dict(begin=100+start, end=100+end))
        semantic = dict(status='scheduled_proc', callback_symbol='grStadium_801D1390',
                        classifier=3, p_link=4, s_link=1)
        edges.append(dict(match_index=mi, original=(ga, pa), native=(nga, npa), world=w, semantic=semantic,
                          original_callback=0x80003000, native_callback=0xabc))
        # Project exact actual entry/return coordinates, as the extractor does.
        for r in rows[start:end]:
            k = r['kind']; seq = 100+r['sequence']
            if k == 3:
                events.append(dict(event='allocation_return', return_sequence=seq,
                                   enter_sequence=seq-1, allocation_generation=og if r['pool_kind']==1 else op,
                                   pool_kind=r['pool_kind'], source_address=hex(ga if r['pool_kind']==1 else pa)))
            elif k in (5,7,8,9,10,15):
                event = {5:'free_return',7:'setup_registration',8:'proc_link',9:'proc_scheduler_unlink',
                         10:'proc_owner_unlink',15:'proc_remove_request'}[k]
                e = dict(event=event, enter_sequence=100+r['call_index'], return_sequence=seq,
                         proc_generation=op, gobj_generation=og, source_proc_address=hex(pa),
                         source_gobj_address=hex(ga), callback_symbol='grStadium_801D1390', priority=1,
                         callback_address='0x80003000')
                if k == 5:
                    e.update(allocation_generation=og if r['pool_kind']==1 else op,
                             allocation_identity_status='active_current_epoch')
                events.append(e)
    extraction = dict(schema='melee-web-original-gobjproc-registration-extraction', version=1,
                      events=events, summary={'reset_residual_count':0})
    status = dict(schema='melee-web-source-registration-status-v1',armed=True,sealed=True,invalid=False,
                  overflow=False,active_world='0',pending_setup=False,count=len(rows),row_capacity=65536,
                  slot_capacity=8192,storage_bytes=10000000,worlds=2,unselected_proc_hook_calls=11)
    return extraction, bounds, rows, status, edges


class RegistrationComparisonTests(unittest.TestCase):
    def test_modeled_two_lives_pointer_reuse_and_nested_actual_operations(self):
        result = m.compare(*fixture())
        self.assertEqual(result['selected_owners'], 2)
        self.assertEqual(result['selected_edges'], 32)
        self.assertFalse(result['C3'])
        self.assertFalse(result['allocation_lifetime_parity'])

    def test_missing_birth_and_ambiguous_reset_free_refuse(self):
        for mutation in ['birth','alias']:
            ex,b,r,s,e = fixture()
            if mutation == 'birth':
                ex['events'] = [x for x in ex['events'] if not(x['event']=='allocation_return' and x['allocation_generation']=='102')]
            else:
                next(x for x in ex['events'] if x['event']=='free_return')['allocation_identity_status']='active_current_epoch_with_prior_residual_alias'
            with self.assertRaises(m.Refusal): m.compare(ex,b,r,s,e)

    def test_callback_priority_and_fighter_semantics_never_use_raw_pointer_equality(self):
        ex,b,r,s,e = fixture()
        e[0]['semantic']['s_link']=2
        with self.assertRaisesRegex(m.Refusal,'priority'): m.compare(ex,b,r,s,e)
        ex,b,r,s,e = fixture()
        next(x for x in r if x['kind']==8)['callback']='0xdef'
        with self.assertRaisesRegex(m.Refusal,'callback'): m.compare(ex,b,r,s,e)

    def test_missing_reordered_and_duplicate_edge_refuse(self):
        for mutation in ['missing','reordered','duplicate']:
            ex,b,r,s,e = fixture()
            event = next(x for x in ex['events'] if x['event']=='proc_scheduler_unlink')
            if mutation=='missing': ex['events'].remove(event)
            elif mutation=='reordered': event['enter_sequence']+=3;event['return_sequence']+=3
            else: ex['events'].append(copy.deepcopy(event))
            with self.assertRaises(m.Refusal): m.compare(ex,b,r,s,e)

    def test_incomplete_overflow_foreign_world_and_bad_nesting_refuse(self):
        for mutation in ['incomplete','overflow','world','nest']:
            ex,b,r,s,e = fixture()
            if mutation=='incomplete': s['sealed']=False
            elif mutation=='overflow': s['overflow']=True
            elif mutation=='world': e[1]['world']=e[0]['world']
            else: next(x for x in r if x['kind']==8)['call_index']=0
            with self.assertRaises(m.Refusal): m.compare(ex,b,r,s,e)

    def test_extra_selected_registration_is_not_silently_skipped(self):
        ex,b,r,s,e = fixture()
        extra=copy.deepcopy(next(x for x in ex['events'] if x['event']=='setup_registration'))
        extra['source_proc_address']='0x80003000';extra['return_sequence']+=1
        ex['events'].append(extra)
        with self.assertRaises(m.Refusal): m.compare(ex,b,r,s,e)

    def test_exact_scene_callback_boundaries_and_missing_return(self):
        enters={1:dict(call=1,function='gm_Scene_Vs_OnEnter',sequence=1),
                2:dict(call=2,function='gm_Scene_Vs_OnExit',sequence=20),
                3:dict(call=3,function='gm_Scene_Vs_OnEnter',sequence=30),
                4:dict(call=4,function='gm_Scene_Vs_OnExit',sequence=50)}
        returns={1:dict(sequence=10),2:dict(sequence=25),3:dict(sequence=40),4:dict(sequence=55)}
        self.assertEqual([x['end'] for x in m.scene_bounds(enters,returns)],[25,55])
        del returns[4]
        with self.assertRaises(m.Refusal): m.scene_bounds(enters,returns)



def residual_fixture():
    ex,b,r,s,e=fixture()
    # Clearly modeled original pool-retirement storage shape; native fixture
    # retains every actual-function-validated nested cleanup edge.
    ex['events']=[x for x in ex['events'] if x['event'] in
                  ('allocation_return','setup_registration','proc_link')]
    for x in ex['events']:
        for key in ('enter_sequence','return_sequence'):
            if x[key]>=b[1]['begin']: x[key]+=100
    b[1]={key:value+100 for key,value in b[1].items()}
    for x in ex['events']:
        if x['event']=='proc_link':
            # Exact extracted original link shape: args/symbol/generation and
            # completed-link facts; callback address/priority are Setup facts.
            for key in ('source_gobj_address','callback_address','priority'): x.pop(key)
            x.update(owner_linked_after_return=True,scheduler_linked_after_return=True)
    for pool,gen in [(1,'101'),(2,'102')]:
        ex['events'].append(dict(event='pool_reinitialize_return',pool_kind=pool,
                                enter_sequence=b[0]['end']+pool*2,
                                return_sequence=b[0]['end']+pool*2+1,
                                reset_residual_generations=[gen]))
    return ex,b,r,s,e


class ScheduledLifetimeTests(unittest.TestCase):
    def test_modeled_semantic_pass_keeps_literal_fail_and_b_dynamic_unavailable(self):
        result=m.compare_scheduled_lifetimes(*residual_fixture(),{'modeled_control':True})
        self.assertEqual(result['raw_literal_registration_comparison']['status'],
                         'FAIL_LITERAL_SELECTED_REGISTRATION_EDGE_ORDER')
        self.assertEqual([x['dynamic_original_reset_observed'] for x in result['retirements']],[True,False])
        self.assertFalse(result['C3'])
        self.assertEqual(result['native_local_cleanup_edges'],20)

    def test_creation_link_missing_duplicate_and_reordered_refuse(self):
        for mutation in ('missing','duplicate','reordered'):
            ex,b,r,s,e=residual_fixture()
            link=next(x for x in ex['events'] if x['event']=='proc_link')
            if mutation=='missing': ex['events'].remove(link)
            elif mutation=='duplicate': ex['events'].append(copy.deepcopy(link))
            else: link['enter_sequence']+=10;link['return_sequence']+=10
            with self.assertRaises(m.Refusal): m.compare_scheduled_lifetimes(ex,b,r,s,e,{})

    def test_immutable_link_callback_generation_and_priority_refuse(self):
        for key,value in [('callback_symbol','Fighter_8006A360'),('gobj_generation','999'),('owner_linked_after_return',False)]:
            ex,b,r,s,e=residual_fixture()
            next(x for x in ex['events'] if x['event']=='proc_link')[key]=value
            with self.assertRaises(m.Refusal): m.compare_scheduled_lifetimes(ex,b,r,s,e,{})

    def test_missing_early_or_foreign_pool_retirement_refuses(self):
        for mutation in ('missing','early','pool','Breset'):
            ex,b,r,s,e=residual_fixture()
            reset=next(x for x in ex['events'] if x['event']=='pool_reinitialize_return')
            if mutation=='missing': ex['events'].remove(reset)
            elif mutation=='early': reset['enter_sequence']=b[0]['begin']
            elif mutation=='pool': reset['pool_kind']=2
            else: reset['reset_residual_generations'].append('103')
            with self.assertRaises(m.Refusal): m.compare_scheduled_lifetimes(ex,b,r,s,e,{})

    def test_native_generation_reuse_and_missing_unlink_refuse(self):
        for mutation in ('generation','unlink'):
            ex,b,r,s,e=residual_fixture()
            if mutation=='generation':
                next(x for x in r if x['kind']==3 and x['world_generation']=='31')['object_generation']='1'
            else:
                next(x for x in r if x['kind']==13)['callback']='0xdef'
            with self.assertRaises(m.Refusal): m.compare_scheduled_lifetimes(ex,b,r,s,e,{})

    def test_successor_lr_order_and_missing_boundary_refuse(self):
        expected=[]
        for mi in range(2):
            for i,name in enumerate(('vs_exit','vs_exit_return','results_enter','return_css')):
                expected.append(dict(match_index=mi,boundary=name,sequence=mi*100+i,
                                     actual_pc='0x80177368',actual_lr='0x801a40e8'))
        m.validate_successors(expected,expected)
        for mutation in ('lr','order','missing'):
            actual=copy.deepcopy(expected)
            if mutation=='lr': actual[6]['actual_lr']='0x801a40ec'
            elif mutation=='order': actual[5],actual[6]=actual[6],actual[5]
            else: actual.pop()
            with self.assertRaises(m.Refusal): m.validate_successors(actual,expected)

    def test_unreviewed_or_missing_source_proof_refuses_before_imports(self):
        import tempfile
        with tempfile.TemporaryDirectory() as d:
            path=Path(d)/'proof.json';path.write_text('{"approved":true}')
            with self.assertRaisesRegex(m.Refusal,'unreviewed'): m.admit_retirement_proof(path,{}, {})
            path.unlink()
            with self.assertRaisesRegex(m.Refusal,'regular'): m.admit_retirement_proof(path,{}, {})

if __name__ == '__main__': unittest.main()
