-- Type 13 is springs/dampers. Keep existing upgrade and garage part IDs.
WITH kit(level,name,description) AS (
  VALUES (5,'_&1888612608','_&1888612609'),(6,'_&1888571392','_&1888567232')
)
INSERT INTO Upgrades(TypeId,IsException,Name,Level,Description,IconPath,ImagePath)
SELECT '13',0,k.name,k.level,k.description,r.IconPath,r.ImagePath
FROM kit k JOIN Upgrades r ON r.id=(SELECT min(id) FROM Upgrades WHERE TypeId='13' AND Level=3)
WHERE NOT EXISTS (SELECT 1 FROM Upgrades u WHERE u.TypeId='13' AND u.Level=k.level);
UPDATE Upgrades SET Name='_&1888571392',Description='_&1888567232'
WHERE TypeId='13' AND Level=6 AND (Name IS NOT '_&1888571392' OR Description IS NOT '_&1888567232');
UPDATE Upgrades SET Name='_&1888612608',Description='_&1888612609'
WHERE TypeId='13' AND Level=5 AND (Name IS NOT '_&1888612608' OR Description IS NOT '_&1888612609');

-- Temporary objects live through all mods' SQL transactions on this build
-- connection. The game receives ordinary parts and ordinary physics records.
CREATE TEMP TABLE da_work(car INTEGER PRIMARY KEY);
CREATE TEMP TABLE da_made(car INTEGER,level INTEGER,part INTEGER UNIQUE,
                          block INTEGER UNIQUE,PRIMARY KEY(car,level));
CREATE TEMP TABLE da_displaced(car INTEGER PRIMARY KEY);
CREATE TEMP TABLE da_move(car,level,old_block,new_block);
CREATE TEMP TABLE da_levels(level INTEGER PRIMARY KEY,price INTEGER);
INSERT INTO da_levels VALUES(5,3500),(6,4200);

-- Measured FD / SLR front and rear spring and damper profiles from the
-- installed 23.5-unit reference chassis, with 48% on the front axle.
CREATE TEMP TABLE da_profile(level,axle,reference_load,spring,min_s,max_s,bump,rebound,min_d,max_d,
                             toe,camber,caster,lift);
INSERT INTO da_profile VALUES
 (5,0,11.28,58.4679192865,58.4679192865,308.5378326465,4.8440439725,7.7442995994,1.5426891632,30.8537832646,0,-4,7,0),
 (5,1,12.22,49.9702731457,49.9702731457,362.1034285920,4.6530290574,7.4593306290,1.8105171430,36.2103428592,0,0,5,0),
 (6,0,11.28,90,58.4679192865,308.5378326465,7,11,1.5426891632,30.8537832646,0,-4.5,8,0.012),
 (6,1,12.22,80,49.9702731457,362.1034285920,5.8,9,1.8105171430,36.2103428592,0.05,-1,0,0.017);

-- Pick a complete native pair. A late-arriving race part outranks a stock
-- one. Only records generated on this connection can be refreshed.
CREATE TEMP VIEW da_source AS
SELECT w.car,s.*,c.CurbWeight,c.WeightDistribution FROM da_work w
JOIN Data_Car c ON c.Id=w.car
JOIN List_UpgradeSpringDamper s ON s.Id=(
  SELECT t.Id FROM List_UpgradeSpringDamper t
  JOIN List_SpringDamperPhysics f ON f.SpringDamperPhysicsID=t.FrontSpringDamperPhysicsID
  JOIN List_SpringDamperPhysics r ON r.SpringDamperPhysicsID=t.RearSpringDamperPhysicsID
  WHERE t.Ordinal=w.car AND (t.IsStock=1 OR t.Level BETWEEN 0 AND 4)
  ORDER BY (t.Level=3) DESC,t.IsStock DESC,t.Level DESC,t.Id LIMIT 1);

-- Allocate a private pair of physics IDs. Check the actual tables, including
-- existing third-party IDs. Both parts retain the usual car*1000+tier ID if free.
CREATE TEMP VIEW da_slots AS
WITH RECURSIVE candidates(car,level,block) AS (
  SELECT s.car,k.level,1100000000+8*(s.car%10000000)+2*(k.level-5)
  FROM da_source s CROSS JOIN da_levels k
  WHERE NOT EXISTS (SELECT 1 FROM List_UpgradeSpringDamper t
                    WHERE t.Ordinal=s.car AND t.Level=k.level AND t.IsStock=0)
  UNION ALL
  SELECT car,level,block+4 FROM candidates
  WHERE EXISTS (SELECT 1 FROM List_SpringDamperPhysics WHERE SpringDamperPhysicsID BETWEEN block AND block+1)
     OR EXISTS (SELECT 1 FROM List_UpgradeSpringDamper WHERE Id=block)
     OR EXISTS (SELECT 1 FROM da_made m WHERE m.block=candidates.block)
)
SELECT car,level,max(block) AS block FROM candidates GROUP BY car,level;

CREATE TEMP VIEW da_axles AS
SELECT m.block+t.axle AS new_id,s.car,
       CASE t.axle WHEN 0 THEN s.FrontSpringDamperPhysicsID ELSE s.RearSpringDamperPhysicsID END AS source_id,
       CASE WHEN s.CurbWeight>0 AND s.WeightDistribution>0 AND s.WeightDistribution<1
            THEN s.CurbWeight*(CASE t.axle WHEN 0 THEN s.WeightDistribution ELSE 1-s.WeightDistribution END)/t.reference_load
            ELSE 1.0 END AS load_ratio,t.*
FROM da_source s JOIN da_made m ON m.car=s.car JOIN da_profile t ON t.level=m.level;

CREATE TEMP TRIGGER da_fill AFTER INSERT ON da_work BEGIN
  INSERT INTO da_made(car,level,part,block)
  SELECT f.car,f.level,
         CASE WHEN f.car BETWEEN 1 AND 2147483 AND NOT EXISTS
                    (SELECT 1 FROM List_UpgradeSpringDamper WHERE Id=f.car*1000+f.level)
              THEN f.car*1000+f.level ELSE f.block END,f.block
  FROM da_slots f WHERE f.car=NEW.car;
  INSERT OR REPLACE INTO List_SpringDamperPhysics(
      SpringDamperPhysicsID,Ordinal,SuspensionPhysicsTypeID,UseProgressiveSpring,
      DefRideHeight,MinRideHeight,MaxRideHeight,MaxCompressHeight,
      DefSpringRate,MinSpringRate,MaxSpringRate,DefDampenBumpRate,MinDampenBumpRate,MaxDampenBumpRate,
      DampenBumpClamp,DefDampenReboundRate,MinDampenReboundRate,MaxDampenReboundRate,DampenReboundClamp,
      BumpstopStiffness,BumpstopDamping,ChassisPivotX,ChassisPivotY,TirePivotX,
      CamberChangeRatioCompression,CamberChangeRatioExpansion,StaticToe,StaticCamber,Caster,
      SteerRotAxisTireOffsetX,YMaxSpringVel,MaxStretchDeltaFromRideHeight)
  SELECT a.new_id,a.car,p.SuspensionPhysicsTypeID,p.UseProgressiveSpring,
      max(p.MinRideHeight,min(p.MaxRideHeight,p.DefRideHeight+a.lift)),p.MinRideHeight,p.MaxRideHeight,p.MaxCompressHeight,
      a.spring*a.load_ratio,a.min_s*a.load_ratio,a.max_s*a.load_ratio,
      a.bump*a.load_ratio,a.min_d*a.load_ratio,a.max_d*a.load_ratio,p.DampenBumpClamp,
      a.rebound*a.load_ratio,a.min_d*a.load_ratio,a.max_d*a.load_ratio,p.DampenReboundClamp,
      p.BumpstopStiffness,p.BumpstopDamping,p.ChassisPivotX,p.ChassisPivotY,p.TirePivotX,
      p.CamberChangeRatioCompression,p.CamberChangeRatioExpansion,a.toe,a.camber,a.caster,
      p.SteerRotAxisTireOffsetX,p.YMaxSpringVel,p.MaxStretchDeltaFromRideHeight
  FROM da_axles a JOIN List_SpringDamperPhysics p ON p.SpringDamperPhysicsID=a.source_id WHERE a.car=NEW.car;
  INSERT OR REPLACE INTO List_UpgradeSpringDamper(
      Id,Ordinal,Level,ManufacturerID,MassDiff,DragScale,WindInstabilityScale,Price,IsStock,
      SuspensionDamageModDefID,TiresDamageModDefID,FrontSpringDamperPhysicsID,RearSpringDamperPhysicsID)
  SELECT m.part,s.car,k.level,s.ManufacturerID,s.MassDiff,s.DragScale,s.WindInstabilityScale,k.price,0,
         s.SuspensionDamageModDefID,s.TiresDamageModDefID,m.block,m.block+1
  FROM da_source s JOIN da_made m ON m.car=s.car JOIN da_levels k ON k.level=m.level WHERE s.car=NEW.car;
  DELETE FROM da_work WHERE car=NEW.car;
END;

-- A pack that supplies its own kit later in this build takes precedence.
CREATE TEMP TRIGGER da_pack_part BEFORE INSERT ON main.List_UpgradeSpringDamper
WHEN NOT EXISTS(SELECT 1 FROM da_work)
 AND EXISTS(SELECT 1 FROM da_made WHERE part=NEW.Id OR (car=NEW.Ordinal AND level=NEW.Level AND NEW.IsStock=0))
BEGIN
  INSERT OR IGNORE INTO da_displaced SELECT car FROM da_made
      WHERE part=NEW.Id OR (car=NEW.Ordinal AND level=NEW.Level AND NEW.IsStock=0);
  DELETE FROM List_UpgradeSpringDamper WHERE Id IN (
    SELECT part FROM da_made WHERE part=NEW.Id OR (car=NEW.Ordinal AND level=NEW.Level AND NEW.IsStock=0));
  DELETE FROM List_SpringDamperPhysics WHERE SpringDamperPhysicsID IN (
    SELECT block+a.axle FROM da_made m JOIN da_profile a ON m.level=a.level
    WHERE m.part=NEW.Id OR (m.car=NEW.Ordinal AND m.level=NEW.Level AND NEW.IsStock=0))
    AND SpringDamperPhysicsID NOT IN (NEW.FrontSpringDamperPhysicsID,NEW.RearSpringDamperPhysicsID)
    AND NOT EXISTS(SELECT 1 FROM List_UpgradeSpringDamper WHERE
          FrontSpringDamperPhysicsID=List_SpringDamperPhysics.SpringDamperPhysicsID OR
          RearSpringDamperPhysicsID=List_SpringDamperPhysics.SpringDamperPhysicsID);
  DELETE FROM da_made WHERE part=NEW.Id OR (car=NEW.Ordinal AND level=NEW.Level AND NEW.IsStock=0);
END;
CREATE TEMP TRIGGER da_car AFTER INSERT ON main.Data_Car BEGIN
    INSERT INTO da_work VALUES(NEW.Id);
END;
-- If a later pack explicitly assigns a physics ID from our private range,
-- move the generated pair before the incoming INSERT checks its primary key.
CREATE TEMP TRIGGER da_claim_axle BEFORE INSERT ON main.List_SpringDamperPhysics
WHEN NOT EXISTS(SELECT 1 FROM da_work) AND EXISTS(
  SELECT 1 FROM da_made WHERE NEW.SpringDamperPhysicsID BETWEEN block AND block+1)
BEGIN
  INSERT INTO da_move
  SELECT car,level,block,(
    WITH RECURSIVE slots(n) AS (
      SELECT m.block+4 UNION ALL SELECT n+4 FROM slots
      WHERE EXISTS(SELECT 1 FROM List_SpringDamperPhysics WHERE SpringDamperPhysicsID BETWEEN n AND n+1)
         OR EXISTS(SELECT 1 FROM da_made WHERE block=n)
    ) SELECT max(n) FROM slots)
  FROM da_made m WHERE NEW.SpringDamperPhysicsID BETWEEN m.block AND m.block+1;
  UPDATE da_made SET block=(SELECT new_block FROM da_move) WHERE block=(SELECT old_block FROM da_move);
  INSERT INTO da_work SELECT car FROM da_move;
  DELETE FROM List_SpringDamperPhysics WHERE SpringDamperPhysicsID=NEW.SpringDamperPhysicsID
     OR (SpringDamperPhysicsID BETWEEN (SELECT old_block FROM da_move) AND (SELECT old_block+1 FROM da_move)
         AND NOT EXISTS(SELECT 1 FROM List_UpgradeSpringDamper s WHERE
          s.FrontSpringDamperPhysicsID=List_SpringDamperPhysics.SpringDamperPhysicsID OR
          s.RearSpringDamperPhysicsID=List_SpringDamperPhysics.SpringDamperPhysicsID));
  DELETE FROM da_move;
END;
CREATE TEMP TRIGGER da_part AFTER INSERT ON main.List_UpgradeSpringDamper
WHEN NOT EXISTS(SELECT 1 FROM da_work)
BEGIN
  INSERT INTO da_work SELECT car FROM da_displaced
   UNION SELECT NEW.Ordinal WHERE NEW.IsStock=1 OR NEW.Level BETWEEN 0 AND 4;
  DELETE FROM da_displaced;
END;
CREATE TEMP TRIGGER da_physics AFTER INSERT ON main.List_SpringDamperPhysics
WHEN NOT EXISTS(SELECT 1 FROM da_work)
BEGIN
  INSERT INTO da_work SELECT DISTINCT Ordinal FROM List_UpgradeSpringDamper
   WHERE (IsStock=1 OR Level BETWEEN 0 AND 4) AND
    (FrontSpringDamperPhysicsID=NEW.SpringDamperPhysicsID OR RearSpringDamperPhysicsID=NEW.SpringDamperPhysicsID);
END;
CREATE TEMP TRIGGER da_mass AFTER UPDATE OF CurbWeight,WeightDistribution ON main.Data_Car
WHEN EXISTS(SELECT 1 FROM da_made WHERE car=NEW.Id)
BEGIN INSERT INTO da_work VALUES(NEW.Id); END;
CREATE TEMP TRIGGER da_part_edit AFTER UPDATE OF FrontSpringDamperPhysicsID,RearSpringDamperPhysicsID,Level,IsStock ON main.List_UpgradeSpringDamper
WHEN NOT EXISTS(SELECT 1 FROM da_work) AND (NEW.IsStock=1 OR NEW.Level BETWEEN 0 AND 4)
BEGIN INSERT INTO da_work VALUES(NEW.Ordinal); END;
CREATE TEMP TRIGGER da_axle_edit AFTER UPDATE ON main.List_SpringDamperPhysics
WHEN NOT EXISTS(SELECT 1 FROM da_work)
BEGIN
  INSERT INTO da_work SELECT DISTINCT Ordinal FROM List_UpgradeSpringDamper
   WHERE (IsStock=1 OR Level BETWEEN 0 AND 4) AND
    (FrontSpringDamperPhysicsID=NEW.SpringDamperPhysicsID OR RearSpringDamperPhysicsID=NEW.SpringDamperPhysicsID);
END;
INSERT INTO da_work SELECT Id FROM Data_Car c WHERE EXISTS(
  SELECT 1 FROM da_levels k WHERE NOT EXISTS(SELECT 1 FROM List_UpgradeSpringDamper s
    WHERE s.Ordinal=c.Id AND s.Level=k.level AND s.IsStock=0));
