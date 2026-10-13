"""重み付き骨接触の領域と生成形式を検査する。"""
from __future__ import annotations
import importlib.util
import json
import math
from pathlib import Path
import sys
import tempfile
import unittest
from contextlib import redirect_stdout
from io import StringIO
from unittest.mock import patch
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from fit_secondary_motion_contact_points import fit_contact_points, main

def binding(bone,name,weight,position,group='skirtChild'):
    return dict(bone=bone,name=name,weight=weight,localPosition=position,group=group)
def point(identifier,bindings):
    return dict(type='gkcore_skin_binding_point',positionId=identifier,bindings=bindings)

class ContactPointFitTests(unittest.TestCase):
    def test_root_offset_selection_requires_opt_in_and_nonzero_merged_offset(self):
        root=point(1,[binding(8,'SkirtRoot',1,[0,1,0],'skirtRoot')])
        zero_root=point(2,[binding(8,'SkirtRoot',1,[0,0,0],'skirtRoot')])
        cancelled_root=point(3,[binding(8,'SkirtRoot',.5,[0,1,0],'skirtRoot'),binding(8,'SkirtRoot',.5,[0,-1,0],'skirtRoot')])
        legacy,legacy_report=fit_contact_points([root],.01,.002)
        self.assertEqual(legacy,[])
        self.assertFalse(legacy_report['includeRootOffsets'])
        contacts,report=fit_contact_points([root,zero_root,cancelled_root],.01,.002,minimum_moving_weight=.99,include_root_offsets=True)
        self.assertEqual([row['positionIds'] for row in contacts],[[1]])
        self.assertEqual(report['excludedPositionIds'],[2,3])
        self.assertTrue(report['includeRootOffsets'])

    def test_root_child_fixed_weights_are_retained_and_cover_independent_rotations(self):
        records=[
            point(11,[binding(1,'SkirtRoot',.2,[0,.1,0],'skirtRoot'),binding(2,'SkirtA.001',.5,[.1,0,0]),binding(9,'Hips',.3,[0,0,0],'bodyShapeAncestor')]),
            point(12,[binding(1,'SkirtRoot',.2,[.01,.12,0],'skirtRoot'),binding(2,'SkirtA.001',.5,[.12,.01,0]),binding(9,'Hips',.3,[.002,0,0],'bodyShapeAncestor')]),
        ]
        contacts,report=fit_contact_points(records,.05,.001,minimum_moving_weight=.7,include_root_offsets=True)
        self.assertEqual(len(contacts),1)
        anchors=contacts[0]['anchors']
        self.assertEqual([anchor['bone'] for anchor in anchors],[1,2,9])
        self.assertEqual([anchor['weight'] for anchor in anchors],[.2,.5,.3])
        self.assertEqual(sum(anchor['weight'] for anchor in anchors),1)
        self.assertEqual(report['selectedPoints'],2)
        angles=(.7,-1.1,0.4); translations=((-1.,.2,.5),(1.,-.3,-.7),(.3,.1,-.2))
        def transform(position,index):
            cosine=math.cos(angles[index]); sine=math.sin(angles[index]); translation=translations[index]
            return [cosine*position[0]-sine*position[1]+translation[0],sine*position[0]+cosine*position[1]+translation[1],position[2]+translation[2]]
        centre=[sum(anchor['weight']*transform(anchor['offset'],index)[axis] for index,anchor in enumerate(anchors)) for axis in range(3)]
        for record in records:
            actual=[sum(binding_row['weight']*transform(binding_row['localPosition'],index)[axis] for index,binding_row in enumerate(record['bindings'])) for axis in range(3)]
            self.assertLessEqual(math.dist(actual,centre),contacts[0]['radius']+1e-12)
        reversed_contacts,reversed_report=fit_contact_points(list(reversed(records)),.05,.001,minimum_moving_weight=.7,include_root_offsets=True)
        self.assertEqual(contacts,reversed_contacts)
        self.assertEqual(report,reversed_report)

    def test_root_weight_counts_only_after_same_bone_bindings_are_merged(self):
        rows=[
            point(21,[binding(1,'SkirtRoot',.5,[0,1,0],'skirtRoot'),binding(1,'SkirtRoot',.5,[0,-1,0],'skirtRoot')]),
            point(22,[binding(1,'SkirtRoot',.5,[0,1,0],'skirtRoot'),binding(1,'SkirtRoot',.5,[0,1,0],'skirtRoot')]),
        ]
        contacts,report=fit_contact_points(rows,.01,.002,minimum_moving_weight=.99,include_root_offsets=True)
        self.assertEqual([row['positionIds'] for row in contacts],[[22]])
        self.assertEqual(report['excludedPositionIds'],[21])
        inconsistent=point(23,[binding(1,'SkirtRoot',.5,[0,1,0],'skirtRoot'),binding(1,'SkirtRoot',.5,[0,1,0],'skirtChild')])
        with self.assertRaises(ValueError):
            fit_contact_points([inconsistent],.01,.002,include_root_offsets=True)
        cross_record_inconsistent=[point(24,[binding(1,'SkirtRoot',1,[0,1,0],'skirtRoot')]),point(25,[binding(1,'SkirtRoot',1,[0,1,0],'skirtChild')])]
        with self.assertRaises(ValueError):
            fit_contact_points(cross_record_inconsistent,.01,.002,include_root_offsets=True)

    def test_selected_ids_filters_root_candidates_and_reports_unselected_ids(self):
        rows=[point(index,[binding(1,'SkirtRoot',1,[0,1,0],'skirtRoot')]) for index in (31,32)]
        contacts,report=fit_contact_points(rows,.01,.002,include_root_offsets=True,selected_ids={32})
        self.assertEqual([row['positionIds'] for row in contacts],[[32]])
        self.assertEqual(report['excludedPositionIds'],[31])

    def test_cli_exposes_root_offset_opt_in_and_records_setting(self):
        rows=[point(41,[binding(1,'SkirtRoot',1,[0,1,0],'skirtRoot')])]
        with tempfile.TemporaryDirectory() as directory:
            source=Path(directory)/'bindings.json'
            output=Path(directory)/'contacts.txt'
            source.write_text(json.dumps(rows),encoding='utf-8')
            with patch('sys.argv',['fit_secondary_motion_contact_points.py',str(source),str(output),'--include-root-offsets']),redirect_stdout(StringIO()):
                main()
            report=json.loads(output.with_suffix('.fit.json').read_text(encoding='utf-8'))['report']
            self.assertTrue(report['includeRootOffsets'])
            self.assertEqual(report['selectedPoints'],1)

    def test_retains_all_bone_weights_and_covers_distinct_rotations(self):
        records=[point(1,[binding(3,'SkirtA.001',.6,[.0,.0,.0]),binding(7,'SkirtB.002',.4,[.1,.0,.0])]),point(2,[binding(3,'SkirtA.001',.6,[.02,.01,.0]),binding(7,'SkirtB.002',.4,[.08,.01,.0])])]
        contacts,report=fit_contact_points(records,.05,.001)
        self.assertEqual(len(contacts),1)
        anchors=contacts[0]['anchors']
        self.assertEqual([x['weight'] for x in anchors],[.6,.4])
        self.assertEqual(report['selectedPoints'],2)
        # 独立した二つの回転と移動の後も、同じweightの代表球が元の混合位置を覆う。
        angles=(.7,-1.1); translations=((-1.,.2,.5),(1.,-.3,-.7))
        def transform(p,k):
            c=math.cos(angles[k]); s=math.sin(angles[k]); t=translations[k]
            return [c*p[0]-s*p[1]+t[0],s*p[0]+c*p[1]+t[1],p[2]+t[2]]
        centre=[sum(a['weight']*transform(a['offset'],k)[axis] for k,a in enumerate(anchors)) for axis in range(3)]
        for row in records:
            actual=[sum(a['weight']*transform(a['localPosition'],k)[axis] for k,a in enumerate(row['bindings'])) for axis in range(3)]
            self.assertLessEqual(math.dist(actual,centre),contacts[0]['radius']+1e-12)
        reversed_contacts,reversed_report=fit_contact_points(list(reversed(records)),.05,.001)
        self.assertEqual(contacts,reversed_contacts); self.assertEqual(report,reversed_report)
    def test_different_weights_are_not_merged_and_fixed_share_is_retained(self):
        rows=[point(1,[binding(1,'SkirtA.001',.995,[0,0,0]),binding(4,'Hips',.005,[0,1,0],'bodyShapeAncestor')]),point(2,[binding(1,'SkirtA.001',.994,[0,0,0]),binding(4,'Hips',.006,[0,1,0],'bodyShapeAncestor')]),point(3,[binding(4,'Hips',1,[0,0,0],'bodyShapeAncestor')])]
        contacts,report=fit_contact_points(rows,.01,.002)
        self.assertEqual(len(contacts),2); self.assertEqual(report['excludedPoints'],1)
        for contact in contacts:
            self.assertEqual(len(contact['anchors']),2)
            self.assertEqual(sum(a['weight'] for a in contact['anchors']),1)
    def test_coalesces_duplicate_bone_bindings_without_dropping_weight(self):
        rows=[point(1,[binding(1,'SkirtA.001',.3,[0,0,0]),binding(1,'SkirtA.001',.3,[2,0,0]),binding(2,'SkirtB.001',.4,[0,0,0])])]
        contacts,_=fit_contact_points(rows,.01,.002)
        self.assertEqual(len(contacts[0]['anchors']),2)
        self.assertEqual(contacts[0]['anchors'][0]['offset'],[1,0,0]); self.assertEqual(contacts[0]['anchors'][0]['weight'],.6)
    def test_rejects_invalid_inputs_and_nonunit_weight_sums(self):
        for rows in ([point(1,[binding(1,'SkirtA.001',.5,[0,0,0])])],[point(1,[binding(1,'SkirtA.001',1,[math.nan,0,0])])],[point(1,[binding(1,'SkirtA.001',1,[0,0,0])]),point(1,[binding(1,'SkirtA.001',1,[0,0,0])])]):
            with self.assertRaises(ValueError):fit_contact_points(rows,.01,.002)

if __name__=='__main__':unittest.main()
