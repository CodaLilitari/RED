# Custom templates for RED's COG template library, from Lily's KnightCop kit
# (Lilitari, for Andrei and KnightCop, November 2026). Merge the rows a set
# needs into the project's master.tpl, parents before children. Parents named
# here but not defined (_decor, _actor, tuskan, +stlaser, ...) are stock.
swphands          none               type=debris move=none collide=0 thingflags=0x10 size=0.001 movesize=0.001 model3d=swpovfr.3do puppet=swclbike.pup timer=10
swpcam            none               type=ghost move=physics collide=0 size=0.001 movesize=0.001 physflags=0x0
+swpgunroot       none               type=ghost move=physics collide=0 timer=0.1 size=0.001 movesize=0.001 physflags=0x0
swp_classic       _decor             thingflags=0x400 model3d=swpclanim.3do size=0.18 movesize=0.18 move=none puppet=swclbike.pup
+swpbolt          +stlaser           vel=(0/14/0) damage=18 mindamage=10 timer=1.8 soundclass=stlaser.snd
+swpshard2        _debris2           model3d=swclsh2.3do size=0.020 movesize=0.020 damage=0 timer=1.8 vel=(0/1.8/1.0) angvel=(120/90/130)
+swpshard1        _debris2           model3d=swclsh1.3do size=0.020 movesize=0.020 damage=0 timer=1.8 vel=(0/1.5/0.7) angvel=(90/120/70)
+swpboom          _explosion         thingflags=0x1 light=0.2 timer=0.8 sprite=detx.spr soundclass=exp_probe.snd typeflags=0x17 damage=0 blasttime=0.7 force=0 maxlight=0.8 range=0.45 debris=+swpshard1 debris=+swpshard2 debris=+swpshard1 debris=+swpshard2
+kcsway none type=ghost move=none collide=0 size=0.001 movesize=0 thingflags=0x0
kcswoop _actor thingflags=0x400400 model3d=ts.3do size=0.18 movesize=0.16 puppet=kcswoop.pup soundclass=none cog=none aiclass=kcswoop.ai mass=180 surfdrag=1.8 airdrag=0.8 staticdrag=0 maxvel=2.8 maxthrust=4.5 maxrotvel=100 maxrotthrust=100 health=120 maxhealth=120 typeflags=0x80000 eyeoffset=(0/0/0.1)
+kcsbody none type=corpse move=physics collide=1 thingflags=0x0 model3d=kcsbody.3do puppet=kcswoop.pup size=0.10 movesize=0.10 mass=70 physflags=0x1251 surfdrag=5 airdrag=0.7 maxrotvel=240 orientspeed=0 timer=20
+kcstusk tuskan cog=kcs_tusk.cog
+kcsfrag1 _debris2 model3d=tsbk_1.3do size=0.025 movesize=0.02 damage=0 timer=2.8 mass=2 physflags=0x221 angvel=(87/123/91)
+kcsfrag2 _debris2 model3d=tsbk_2.3do size=0.025 movesize=0.02 damage=0 timer=2.8 mass=2 physflags=0x221 angvel=(104/136/102)
+kcsfrag3 _debris2 model3d=tsbk_3.3do size=0.025 movesize=0.02 damage=0 timer=2.8 mass=2 physflags=0x221 angvel=(121/149/113)
+kcsfrag4 _debris2 model3d=tsbk_4.3do size=0.025 movesize=0.02 damage=0 timer=2.8 mass=2 physflags=0x221 angvel=(138/162/124)
+kcsfrag5 _debris2 model3d=tsbk_5.3do size=0.025 movesize=0.02 damage=0 timer=2.8 mass=2 physflags=0x221 angvel=(155/175/135)
+kcsfrag6 _debris2 model3d=tsbk_6.3do size=0.025 movesize=0.02 damage=0 timer=2.8 mass=2 physflags=0x221 angvel=(172/188/146)
+kcsfrag7 _debris2 model3d=tsbk_7.3do size=0.025 movesize=0.02 damage=0 timer=2.8 mass=2 physflags=0x221 angvel=(189/201/157)
+kcsfrag8 _debris2 model3d=tsbk_8.3do size=0.025 movesize=0.02 damage=0 timer=2.8 mass=2 physflags=0x221 angvel=(206/214/168)
+kcsboom _explosion thingflags=0x1 light=0.3 timer=0.8 sprite=detx.spr soundclass=exp_probe.snd typeflags=0x17 damage=35 blasttime=0.5 force=70 maxlight=0.8 range=0.5
+kckbody none orient=(0/0/0) type=corpse move=physics collide=1 thingflags=0x600 model3d=kckbody.3do puppet=kckbody.pup cog=kckbody.cog size=0.18 movesize=0.18 mass=70 physflags=0x4051 surfdrag=5 airdrag=0.7 maxrotvel=0 orientspeed=0 timer=20
kc_kbow _humanactor thingflags=0x400 model3d=kn_bow.3do size=0.125 movesize=0.125 puppet=kn_bow.pup soundclass=kn_bow.snd cog=kc_bow.cog mass=3000 maxvel=0.6 weapon=+ebow weapon2=+ebowplus health=500 maxhealth=500 maxthrust=1 maxrotthrust=120 typeflags=0x20001 eyeoffset=(0/0.025/0.105) fireoffset=(-0.011776536/0.160811624/0.104518313) aiclass=kn_bow.ai
+kc_kell _humanactor thingflags=0x400 model3d=kd.3do puppet=kg_kell.pup soundclass=kn_melee.snd size=0.125 movesize=0.125 cog=none mass=3000 maxvel=0.16 maxthrust=0.45 maxrotthrust=60 maxrotvel=90 health=500 maxhealth=500 typeflags=0x120000 weapon=+kellbite weapon2=none aiclass=km_roam.ai
+kc_ride none orient=(0/0/0) type=cog move=none collide=0 thingflags=0x0 model3d=km_kell.3do puppet=km_kell.pup soundclass=km_kell.snd size=0.55 movesize=0
+kc_hit none orient=(0/0/0) type=cog move=none collide=3 model3d=kp_hit.3do thingflags=0x10 size=0.458055 movesize=0
