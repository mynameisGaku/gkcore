"""重み付き骨接触の領域と生成形式を検査する。"""
from __future__ import annotations
import importlib.util
import math
from pathlib import Path
import sys
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from fit_secondary_motion_contact_points import fit_contact_points

def binding(bone,name,weight,position,group='skirtChild'):
    return dict(bone=bone,name=name,weight=weight,localPosition=position,group=group)
def point(identifier,bindings):
    return dict(type='gkcore_skin_binding_point',positionId=identifier,bindings=bindings)

class ContactPointFitTests(unittest.TestCase):
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
