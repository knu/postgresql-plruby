
create table Room (
    roomno    varchar(8),
    comment    text
);

create unique index Room_rno on Room using btree (roomno varchar_ops);


create table WSlot (
    slotname    varchar(20),
    roomno    varchar(8),
    slotlink    varchar(20),
    backlink    varchar(20)
);

create unique index WSlot_name on WSlot using btree (slotname varchar_ops);


create table PField (
    name    text,
    comment    text
);

create unique index PField_name on PField using btree (name text_ops);


create table PSlot (
    slotname    varchar(20),
    pfname    text,
    slotlink    varchar(20),
    backlink    varchar(20)
);

create unique index PSlot_name on PSlot using btree (slotname varchar_ops);


create table PLine (
    slotname    varchar(20),
    phonenumber    varchar(20),
    comment    text,
    backlink    varchar(20)
);

create unique index PLine_name on PLine using btree (slotname varchar_ops);


create table Hub (
    name    varchar(14),
    comment    text,
    nslots    int4
);

create unique index Hub_name on Hub using btree (name varchar_ops);


create table HSlot (
    slotname    varchar(20),
    hubname    varchar(14),
    slotno    int4,
    slotlink    varchar(20)
);

create unique index HSlot_name on HSlot using btree (slotname varchar_ops);
create index HSlot_hubname on HSlot using btree (hubname varchar_ops);


create table System (
    name    text,
    comment    text
);

create unique index System_name on System using btree (name text_ops);


create table IFace (
    slotname    varchar(20),
    sysname    text,
    ifname    text,
    slotlink    varchar(20)
);

create unique index IFace_name on IFace using btree (slotname varchar_ops);


create table PHone (
    slotname    varchar(20),
    comment    text,
    slotlink    varchar(20)
);

create unique index PHone_name on PHone using btree (slotname varchar_ops);


-- ************************************************************
-- * AFTER UPDATE on Room
-- *    - If room no changes let wall slots follow
-- ************************************************************
create function tg_room_au() returns trigger as '
    if !$Plans[tg["name"]]
       $Plans[tg["name"]] = PL::Plan.new("update WSlot set roomno = $1
                                              where roomno = $2",
                                              ["varchar", "varchar"]).save
    end
    if new["roomno"] != old["roomno"]
        $Plans[tg["name"]].exec(new["roomno"], old["roomno"])
    end
    PL::OK
' language 'plruby';

create trigger tg_room_au after update
    on Room for each row execute procedure tg_room_au();


-- ************************************************************
-- * AFTER DELETE on Room
-- *    - delete wall slots in this room
-- ************************************************************
create function tg_room_ad() returns trigger as '
    if ! $Plans.key?(tg["name"])
        $Plans[tg["name"]] = PL::Plan.new("delete from WSlot 
                                               where roomno = $1", 
                                          ["varchar"]).save
    end
    $Plans[tg["name"]].exec([old["roomno"]])
    PL::OK
' language 'plruby';

create trigger tg_room_ad after delete
    on Room for each row execute procedure tg_room_ad();


-- ************************************************************
-- * BEFORE INSERT or UPDATE on WSlot
-- *    - Check that room exists
-- ************************************************************
create function tg_wslot_biu() returns trigger as '
    if !$Plans.key?(tg["name"])
        $Plans[tg["name"]] = 
             PL::Plan.new("select count(*) as cnt from Room 
                             where roomno = $1", ["varchar"]).save
    end
    n = $Plans[tg["name"]].exec([new["roomno"]], 1)
    if ! n["cnt"]
        raise "Room #{new[''roomno'']} does not exist"
    end
    PL::OK
' language 'plruby';

create trigger tg_wslot_biu before insert or update
    on WSlot for each row execute procedure tg_wslot_biu();


-- ************************************************************
-- * AFTER UPDATE on PField
-- *    - Let PSlots of this field follow
-- ************************************************************
create function tg_pfield_au() returns trigger as '
    if !$Plans.key?(tg["name"])
        $Plans[tg["name"]] = 
            PL::Plan.new("update PSlot set pfname = $1
                            where pfname = $2", ["text", "text"]).save
    end
    if new["name"] != old["name"]
        $Plans[tg["name"]].exec([new["name"], old["name"]])
    end
    PL::OK
' language 'plruby';

create trigger tg_pfield_au after update
    on PField for each row execute procedure tg_pfield_au();


-- ************************************************************
-- * AFTER DELETE on PField
-- *    - Remove all slots of this patchfield
-- ************************************************************
create function tg_pfield_ad() returns trigger as '
    if ! $Plans.key?(tg["name"])
        $Plans[tg["name"]] = PL::Plan.new("delete from PSlot 
                                             where pfname = $1", 
                                          ["text"]).save
    end
    $Plans[tg["name"]].exec([old["name"]])
    PL::OK
' language 'plruby';

create trigger tg_pfield_ad after delete
    on PField for each row execute procedure tg_pfield_ad();


-- ************************************************************
-- * BEFORE INSERT or UPDATE on PSlot
-- *    - Ensure that our patchfield does exist
-- ************************************************************
create function tg_pslot_biu() returns trigger as '
    if ! $Plans.key?(tg["name"])
        $Plans[tg["name"]] = PL::Plan.new("select count(*) as cnt
                                             from PField where name = $1", 
                                          ["text"]).save
    end
    if ! $Plans[tg["name"]].exec([new["name"]], 1)["cnt"]
        raise "Patchfield #{new[''name'']} does not exist"
    end
    PL::OK
' language 'plruby';

create trigger tg_pslot_biu before insert or update
    on PSlot for each row execute procedure tg_pslot_biu();


-- ************************************************************
-- * AFTER UPDATE on System
-- *    - If system name changes let interfaces follow
-- ************************************************************
create function tg_system_au() returns trigger as '
    if ! $Plans.key?(tg["name"])
        $Plans[tg["name"]] = PL::Plan.new("update IFace set sysname = $1
                                             where sysname = $2", 
                                          ["text", "text"]).save
    end
    if new["name"] != old["name"]
        $Plans[tg["name"]].exec([new["name"], old["name"]])
    end
    PL::OK
' language 'plruby';

create trigger tg_system_au after update
    on System for each row execute procedure tg_system_au();


-- ************************************************************
-- * BEFORE INSERT or UPDATE on IFace
-- *    - set the slotname to IF.sysname.ifname
-- ************************************************************
create function tg_iface_biu() returns trigger as '
    if ! $Plans.key?(tg["name"])
        $Plans[tg["name"]] = PL::Plan.new("select count(*) as cnt from system
                                             where name = $1", ["text"]).save
    end
    if ! $Plans[tg["name"]].exec([new["sysname"]], 1)["cnt"]
        raise "system #{new[''sysname'']} does not exist"
    end
    sname = "IF.#{new[''sysname'']}.#{new[''ifname'']}"
    if sname.size > 20
        raise "IFace slotname #{sname} too long (20 char max)"
    end
    new["slotname"] = sname
    new
' language 'plruby';

create trigger tg_iface_biu before insert or update
    on IFace for each row execute procedure tg_iface_biu();


-- ************************************************************
-- * AFTER INSERT or UPDATE or DELETE on Hub
-- *    - insert/delete/rename slots as required
-- ************************************************************
create function tg_hub_a() returns trigger as '
    if ! $Plans.key?(tg["name"])
        $Plans[tg["name"]] = PL::Plan.new("update HSlot set hubname = $1
                                             where hubname = $2", 
                                          ["varchar", "varchar"]).save
    end
    case tg["op"]
    when PL::INSERT
        hub_adjustslots(new["name"], 0, new["nslots"].to_i)
    when PL::UPDATE
        if old["name"] != new["name"]
            $Plans[tg["name"]].exec([new["name"], old["name"]])
        end
        hub_adjustslots(new["name"], old["nslots"].to_i, new["nslots"].to_i)
    when PL::DELETE
        hub_adjustslots(old["name"], old["nslots"].to_i, 0)
    end
    PL::OK
' language 'plruby';

create trigger tg_hub_a after insert or update or delete
    on Hub for each row execute procedure tg_hub_a();

-- ************************************************************
-- * BEFORE INSERT or UPDATE on HSlot
-- *    - prevent from manual manipulation
-- *    - set the slotname to HS.hubname.slotno
-- ************************************************************
create function tg_hslot_biu() returns trigger as '
    if ! $Plans.key?(tg["name"])
        $Plans[tg["name"]] = 
           PL::Plan.new("select * from Hub where name = $1", ["varchar"]).save
    end
    hubrec = $Plans[tg["name"]].exec([new["hubname"]],1)
    if !hubrec || new["slotno"].to_i < 1 || 
       new["slotno"].to_i > hubrec["nslots"].to_i
        raise "no manual manipulation of HSlot"
    end
    if (tg["og"] == PL::UPDATE) && (new["hubname"] != old["hubname"]) &&
       $Plans[tg["name"]].exec([old["hubname"]], 1)
        raise "no manual manipulation of HSlot"
    end
    sname = "HS.#{new[''hubname'']}.#{new[''slotno'']}"
    if sname.size > 20
        raise "HSlot slotname #{sname} too long (20 char max)"
    end
    new["slotname"] = sname
    new
' language 'plruby';

create trigger tg_hslot_biu before insert or update
    on HSlot for each row execute procedure tg_hslot_biu();


-- ************************************************************
-- * BEFORE DELETE on HSlot
-- *    - prevent from manual manipulation
-- ************************************************************
create function tg_hslot_bd() returns trigger as '
    if ! $Plans.key?(tg["name"])
        $Plans[tg["name"]] = 
           PL::Plan.new("select * from Hub where name = $1", ["varchar"]).save
    end
    hubrec = $Plans[tg["name"]].exec([old["hubname"]],1)
    if !hubrec || old["slotno"].to_i > hubrec["nslots"].to_i
        return PL::OK
    end
    raise "no manual manipulation of HSlot"
' language 'plruby';

create trigger tg_hslot_bd before delete
    on HSlot for each row execute procedure tg_hslot_bd();


-- ************************************************************
-- * BEFORE INSERT on all slots
-- *    - Check name prefix
-- ************************************************************
create function tg_chkslotname() returns trigger as '
    if new["slotname"][0, 2] != args[0]
        raise "slotname must begin with #{args[0]}"
    end
    PL::OK
' language 'plruby';

create trigger tg_chkslotname before insert
    on PSlot for each row execute procedure tg_chkslotname('PS');

create trigger tg_chkslotname before insert
    on WSlot for each row execute procedure tg_chkslotname('WS');

create trigger tg_chkslotname before insert
    on PLine for each row execute procedure tg_chkslotname('PL');

create trigger tg_chkslotname before insert
    on IFace for each row execute procedure tg_chkslotname('IF');

create trigger tg_chkslotname before insert
    on PHone for each row execute procedure tg_chkslotname('PH');


-- ************************************************************
-- * BEFORE INSERT or UPDATE on all slots with slotlink
-- *    - Set slotlink to empty string if NULL value given
-- ************************************************************
create function tg_chkslotlink() returns trigger as '
    if ! new["slotlink"]
        new["slotlink"] = ""
        return new
    end
    PL::OK
' language 'plruby';

create trigger tg_chkslotlink before insert or update
    on PSlot for each row execute procedure tg_chkslotlink();

create trigger tg_chkslotlink before insert or update
    on WSlot for each row execute procedure tg_chkslotlink();

create trigger tg_chkslotlink before insert or update
    on IFace for each row execute procedure tg_chkslotlink();

create trigger tg_chkslotlink before insert or update
    on HSlot for each row execute procedure tg_chkslotlink();

create trigger tg_chkslotlink before insert or update
    on PHone for each row execute procedure tg_chkslotlink();


-- ************************************************************
-- * BEFORE INSERT or UPDATE on all slots with backlink
-- *    - Set backlink to empty string if NULL value given
-- ************************************************************
create function tg_chkbacklink() returns trigger as '
    if ! new["backlink"]
        new["backlink"] = ""
        return new
    end
    PL::OK
' language 'plruby';

create trigger tg_chkbacklink before insert or update
    on PSlot for each row execute procedure tg_chkbacklink();

create trigger tg_chkbacklink before insert or update
    on WSlot for each row execute procedure tg_chkbacklink();

create trigger tg_chkbacklink before insert or update
    on PLine for each row execute procedure tg_chkbacklink();


-- ************************************************************
-- * BEFORE UPDATE on PSlot
-- *    - do delete/insert instead of update if name changes
-- ************************************************************
create function tg_pslot_bu() returns trigger as '
    if ! $Plans.key?("pslot_bu_del")
        $Plans["pslot_bu_del"] =
            PL::Plan.new("delete from PSlot where slotname = $1", 
                         ["varchar"]).save
        $Plans["pslot_bu_ins"] =
            PL::Plan.new("insert into PSlot
                            (slotname, pfname, slotlink, backlink)
                            values ($1, $2, $3, $4)",
                           ["varchar", "varchar", "varchar", "varchar"]).save
    end
    if new["slotname"] != old["slotname"]
       $Plans["pslot_bu_del"].exec([old["slotname"]])
       $Plans["pslot_bu_ins"].exec([new["slotname"], new["pfname"],
                                    new["slotlink"], new["backlink"]])
       return PL::SKIP
    end
    PL::OK
' language 'plruby';

create trigger tg_pslot_bu before update
    on PSlot for each row execute procedure tg_pslot_bu();


-- ************************************************************
-- * BEFORE UPDATE on WSlot
-- *    - do delete/insert instead of update if name changes
-- ************************************************************
create function tg_wslot_bu() returns trigger as '
    if ! $Plans.key?("wslot_bu_del")
        $Plans["wslot_bu_del"] =
            PL::Plan.new("delete from WSlot where slotname = $1", 
                         ["varchar"]).save
        $Plans["wslot_bu_ins"] =
            PL::Plan.new("insert into WSlot
                            (slotname, roomno, slotlink, backlink)
                            values ($1, $2, $3, $4)",
                           ["varchar", "int4", "varchar", "varchar"]).save
    end
    if new["slotname"] != old["slotname"]
       $Plans["wslot_bu_del"].exec([old["slotname"]])
       $Plans["wslot_bu_ins"].exec([new["slotname"], new["roomno"],
                                    new["slotlink"], new["backlink"]])
       return PL::SKIP
    end
    PL::OK
' language 'plruby';

create trigger tg_wslot_bu before update
    on WSlot for each row execute procedure tg_Wslot_bu();


-- ************************************************************
-- * BEFORE UPDATE on PLine
-- *    - do delete/insert instead of update if name changes
-- ************************************************************
create function tg_pline_bu() returns trigger as '
    if ! $Plans.key?("pline_bu_del")
        $Plans["pline_bu_del"] =
            PL::Plan.new("delete from Pline where slotname = $1", 
                         ["varchar"]).save
        $Plans["pline_bu_ins"] =
            PL::Plan.new("insert into Pline
                            (slotname, phonenumber, comment, backlink)
                            values ($1, $2, $3, $4)",
                           ["varchar", "int4", "varchar", "varchar"]).save
    end
    if new["slotname"] != old["slotname"]
       $Plans["pline_bu_del"].exec([old["slotname"]])
       $Plans["pline_bu_ins"].exec([new["slotname"], new["phonenumber"],
                                    new["comment"], new["backlink"]])
       return PL::SKIP
    end
    PL::OK
' language 'plruby';

create trigger tg_pline_bu before update
    on PLine for each row execute procedure tg_pline_bu();


-- ************************************************************
-- * BEFORE UPDATE on IFace
-- *    - do delete/insert instead of update if name changes
-- ************************************************************
create function tg_iface_bu() returns trigger as '
    if ! $Plans.key?("iface_bu_del")
        $Plans["iface_bu_del"] =
            PL::Plan.new("delete from Iface where slotname = $1", 
                         ["varchar"]).save
        $Plans["iface_bu_ins"] =
            PL::Plan.new("insert into Iface
                            (slotname, sysname, ifname, slotlink)
                            values ($1, $2, $3, $4)",
                           ["varchar", "varchar", "varchar", "varchar"]).save
    end
    if new["slotname"] != old["slotname"]
       $Plans["iface_bu_del"].exec([old["slotname"]])
       $Plans["iface_bu_ins"].exec([new["slotname"], new["sysname"],
                                    new["ifname"], new["slotlink"]])
       return PL::SKIP
    end
    PL::OK
' language 'plruby';

create trigger tg_iface_bu before update
    on IFace for each row execute procedure tg_iface_bu();


-- ************************************************************
-- * BEFORE UPDATE on HSlot
-- *    - do delete/insert instead of update if name changes
-- ************************************************************
create function tg_hslot_bu() returns trigger as '
    if ! $Plans.key?("hslot_bu_del")
        $Plans["hslot_bu_del"] =
            PL::Plan.new("delete from Hslot where slotname = $1", 
                         ["varchar"]).save
        $Plans["hslot_bu_ins"] =
            PL::Plan.new("insert into Hslot
                            (slotname, hubname, slotno, slotlink)
                            values ($1, $2, $3, $4)",
                           ["varchar", "varchar", "int4", "varchar"]).save
    end
    if new["slotname"] != old["slotname"]
       $Plans["hslot_bu_del"].exec([old["slotname"]])
       $Plans["hslot_bu_ins"].exec([new["slotname"], new["hubname"],
                                    new["slotno"], new["slotlink"]])
       return PL::SKIP
    end
    PL::OK
' language 'plruby';

create trigger tg_hslot_bu before update
    on HSlot for each row execute procedure tg_hslot_bu();


-- ************************************************************
-- * BEFORE UPDATE on PHone
-- *    - do delete/insert instead of update if name changes
-- ************************************************************
create function tg_phone_bu() returns trigger as '
    if ! $Plans.key?("phone_bu_del")
        $Plans["phone_bu_del"] =
            PL::Plan.new("delete from Phone where slotname = $1", 
                         ["varchar"]).save
        $Plans["phone_bu_ins"] =
            PL::Plan.new("insert into Phone
                            (slotname, comment, slotlink)
                            values ($1, $2, $3)",
                           ["varchar", "varchar", "varchar"]).save
    end
    if new["slotname"] != old["slotname"]
       $Plans["phone_bu_del"].exec([old["slotname"]])
       $Plans["phone_bu_ins"].exec([new["slotname"], new["comment"],
                                    new["slotlink"]])
       return PL::SKIP
    end
' language 'plruby';

create trigger tg_phone_bu before update
    on PHone for each row execute procedure tg_phone_bu();


-- ************************************************************
-- * AFTER INSERT or UPDATE or DELETE on slot with backlink
-- *    - Ensure that the opponent correctly points back to us
-- ************************************************************
create function tg_backlink_a() returns trigger as '
    case tg["op"]
    when PL::INSERT
        if ! new["backlink"].empty?
            backlink_set(new["backlink"], new["slotname"])
        end
    when PL::UPDATE
        if new["backlink"] != old["backlink"]
            if ! old["backlink"].empty?
                backlink_unset(old["backlink"], old["slotname"])
            end
            if ! new["backlink"].empty?
                backlink_set(new["backlink"], new["slotname"])
            end
        else
            if new["slotname"] != old["slotname"] && ! new["backlink"].empty?
                slotlink_set(new["backlink"], new["slotname"])
            end
        end
    when PL::DELETE
        if ! old["backlink"].empty?
            backlink_unset(old["backlink"], old["slotname"]);
        end
    end
    PL::OK
' language 'plruby';


create trigger tg_backlink_a after insert or update or delete
    on PSlot for each row execute procedure tg_backlink_a('PS');

create trigger tg_backlink_a after insert or update or delete
    on WSlot for each row execute procedure tg_backlink_a('WS');

create trigger tg_backlink_a after insert or update or delete
    on PLine for each row execute procedure tg_backlink_a('PL');

-- ************************************************************
-- * AFTER INSERT or UPDATE or DELETE on slot with slotlink
-- *    - Ensure that the opponent correctly points back to us
-- ************************************************************
create function tg_slotlink_a() returns trigger as '
    case tg["op"]
    when PL::INSERT
        if ! new["slotlink"].empty?
            slotlink_set(new["slotlink"], new["slotname"])
        end
    when PL::UPDATE
        if new["slotlink"] != old["slotlink"]
            if ! old["slotlink"].empty?
                stlotlink_unset(old["slotlink"], old["slotname"])
            end
            if ! new["slotlink"].empty?
                slotlink_set(new["slotlink"], new["slotname"])
            end
        else
            if new["slotname"] != old["slotname"] && ! new["slotlink"].empty?
                slotlink_set(new["slotlink"], new["slotname"])
            end
        end
    when PL::DELETE
        if ! old["slotlink"].empty?
            stlotlink_unset(old["slotlink"], old["slotname"])
        end
    end
    PL::OK
' language 'plruby';


create trigger tg_slotlink_a after insert or update or delete
    on PSlot for each row execute procedure tg_slotlink_a('PS');

create trigger tg_slotlink_a after insert or update or delete
    on WSlot for each row execute procedure tg_slotlink_a('WS');

create trigger tg_slotlink_a after insert or update or delete
    on IFace for each row execute procedure tg_slotlink_a('IF');

create trigger tg_slotlink_a after insert or update or delete
    on HSlot for each row execute procedure tg_slotlink_a('HS');

create trigger tg_slotlink_a after insert or update or delete
    on PHone for each row execute procedure tg_slotlink_a('PH');

-- ************************************************************
-- * Describe the backside of a patchfield slot
-- ************************************************************
create function pslot_backlink_view(varchar)
returns text as '
    if ! $Plans.key?("pslot_view")
        $Plans["pslot_view"] =
          PL::Plan.new("select * from PSlot where slotname = $1", 
                       ["varchar"]).save
    end
    rec = $Plans["pslot_view"].exec([args[0]], 1)
    return "" if ! rec
    return "-" if rec["backlink"].empty?
    bltype = rec["backlink"][0, 2]
    case bltype
    when "PL"
        rec = PL.exec("select * from PLine where slotname = ''#{rec[''backlink'']}''", 1)
        retval = "Phone line #{rec[''phonenumber''].strip}"
        if ! rec["comment"].empty?
            retval << " (#{rec[''comment'']})"
        end
        return retval
    when "WS"
        rec = PL.exec("select * from WSlot where slotname = ''#{rec[''backlink'']}''", 1)
        retval = "#{rec[''slotname''].strip} in room "
        retval << rec["roomno"].strip + " -> "
        rec = PL.exec("select wslot_slotlink_view(''#{rec[''slotname'']}''::varchar) as ws_result", 1)
        return retval + rec["ws_result"]
    end
    rec["backlink"]
' language 'plruby';


-- ************************************************************
-- * Describe the front of a patchfield slot
-- ************************************************************
create function pslot_slotlink_view(varchar)
returns text as '
    if ! $Plans.key?("pslot_view")
        $Plans["pslot_view"] =
          PL::Plan.new("select * from PSlot where slotname = $1", 
                       ["varchar"]).save
    end
    rec = $Plans["pslot_view"].exec([args[0]], 1)
    return "" if ! rec
    return "-" if rec["slotlink"].empty?
    case rec["slotlink"][0, 2]
    when "PS"
        retval = rec["slotlink"].strip + " -> "
        rec = PL.exec("select pslot_backlink_view(''#{rec[''slotlink'']}''::varchar) as bac", 1)
        return retval + rec["bac"]
    when "HS"
        comm = PL.exec("select comment from Hub H, HSlot HS
                where HS.slotname = ''#{rec[''slotlink'']}''
                            and H.name = HS.hubname", 1)
        retval = ""
        if ! comm["comment"].empty?
            retval << comm["comment"]
        end
        retval << " slot "
        comm = PL.exec("select slotno from HSlot
                            where slotname = ''#{rec[''slotlink'']}''", 1)
        return retval + comm["slotno"]
    end
    rec["slotlink"]
' language 'plruby';


-- ************************************************************
-- * Describe the front of a wall connector slot
-- ************************************************************
create function wslot_slotlink_view(varchar)
returns text as '
    if ! $Plans.key?("wslot_view")
        $Plans["wslot_view"] =
          PL::Plan.new("select * from WSlot where slotname = $1", 
                       ["varchar"]).save
    end
    rec = $Plans["wslot_view"].exec([args[0]], 1)
    return "" if ! rec
    return "-" if rec["slotlink"].empty?
    case rec["slotlink"][0, 2]
    when "PH"
        rec = PL.exec("select * from PHone where slotname = ''#{rec[''slotlink'']}''", 1)
        retval = "Phone " + rec["slotname"].strip
        if ! rec["comment"].empty?
            retval << " (#{rec[''comment'']})"
        end
        return retval
    when "IF"
        ifrow = PL.exec("select * from IFace where slotname = ''#{rec[''slotlink'']}''", 1)
        syrow = PL.exec("select * from System where name = ''#{ifrow[''sysname'']}''", 1)
        retval = syrow["name"] + " IF " + ifrow["ifname"]
        if ! syrow["comment"].empty?
            retval << " (#{syrow[''comment'']})"
        end
        return retval
    end
    rec["slotlink"]
' language 'plruby';



-- ************************************************************
-- * View of a patchfield describing backside and patches
-- ************************************************************
create view Pfield_v1 as select PF.pfname, PF.slotname,
    pslot_backlink_view(PF.slotname) as backside,
    pslot_slotlink_view(PF.slotname) as patch
    from PSlot PF;


--
--
--

create table plruby_singleton_methods (
    name varchar(60),
    args varchar(60),
    body text,
    comment text
);


create unique index plr_s_m_i on plruby_singleton_methods (name);

insert into plruby_singleton_methods values
('hub_adjustslots', 'name, oldsl, newsl', '
    if ! $Plans.key?("hub_adjust_del")
        $Plans["hub_adjust_del"] = 
            PL::Plan.new("delete from HSlot 
                            where hubname = $1 and slotno > $2",
                           ["varchar", "int4"]).save
        $Plans["hub_adjust_ins"] =
            PL::Plan.new("insert into HSlot (slotname, hubname, slotno, slotlink)
                            values ($1, $2, $3, $4)",
                            ["varchar", "varchar", "int4", "varchar"]).save
    end
    return if oldsl == newsl
    if newsl < oldsl
        $Plans["hub_adjust_del"].exec([name, newsl])
    else
        (oldsl+1).upto(newsl) do |x|
            $Plans["hub_adjust_ins"].exec(["HS.dummy", name, x, ""])
        end
    end
', 'Support function to add/remove slots of Hub');

insert into plruby_singleton_methods values
('backlink_set', 'myname, blname', '
    mytype = myname[0, 2]
    link = mytype + blname[0, 2]
    if link == "PLPL"
        raise "backlink between two phone lines does not make sense"
    end
    if link == "PLWS" || link == "WSPL"
        raise "direct link of phone line to wall slot not permitted"
    end
    rel = case mytype
          when "PS"
              "PSlot"
          when "WS"
              "WSlot"
          when "PL"
              "PLine"
          else
              raise "illegal backlink beginning with #{mytype}"
          end
    rec = PL.exec("select * from #{rel} where slotname = ''#{myname}''", 1)
    if ! rec
        raise "#{myname} does not exist in #{rel}"
    end
    if rec["backlink"] != blname
       PL.exec("update #{rel} set backlink = ''#{blname}''
                    where slotname = ''#{myname}''")
    end
', ' Support function to set the opponents backlink field
 if it does not already point to the requested slot');

insert into plruby_singleton_methods values
('backlink_unset' , 'myname, blname', '
    rel = case myname[0, 2]
          when "PS"
              "PSlot"
          when "WS"
              "WSlot"
          when "PL"
              "PLine"
          else
              raise "illegal backlink beginning with #{myname[0, 2]}"
          end
    rec = PL.exec("select * from #{rel} where slotname = ''#{myname}''", 1)
    return if ! rec
    if rec["backlink"] == blname
       PL.exec("update #{rel} set backlink = ''''
                    where slotname = ''#{myname}''")
    end
', ' Support function to clear out the backlink field if
 it still points to specific slot');

insert into plruby_singleton_methods values
('slotlink_set', 'myname, blname', '
    mytype = myname[0, 2]
    link = mytype + blname[0, 2]
    if link == "PHPH"
        raise "slotlink between two phones does not make sense"
    end
    if link == "PHHS" || link == "HSPH"
        raise "link of phone to hub does not make sense"
    end
    if link == "PHIF" || link == "IFPH"
        raise "link of phone to hub does not make sense"
    end
    if link == "PSWS" || link == "WSPS"
        raise "slotlink from patchslot to wallslot not permitted"
    end
    rel = case mytype
          when "PS"
              "PSlot"
          when "WS"
              "WSlot"
          when "IF"
              "IFace"
          when "HS"
              "HSlot"
          when "PH"
              "PHone"
          else
              raise "illegal slotlink beginning with <#{mytype}>"
          end
    rec = PL.exec("select * from #{rel} where slotname = ''#{myname}''", 1)
    if ! rec
        raise "#{myname} does not exists in #{rel}"
    end
    if rec["slotlink"] != blname
       PL.exec("update #{rel} set slotlink = ''#{blname}''
                    where slotname = ''#{myname}''")
    end
', ' Support function to set the opponents slotlink field
 if it does not already point to the requested slot');

insert into plruby_singleton_methods values
('slotlink_unset', 'myname, blname', '
    rel = case myname[0, 2]
          when "PS"
              "PSlot"
          when "WS"
              "WSlot"
          when "IF"
              "IFace"
          when "HS"
              "HSlot"
          when "PH"
              "PHone"
          else
              raise "illegal slotlink beginning with #{myname[0, 2]}"
          end
    rec = PL.exec("select * from #{rel} where slotname = ''#{myname}''", 1)
    return if ! rec
    if rec["slotlink"] == blname
       PL.exec("update #{rel} set slotlink = ''''
                    where slotname = ''#{myname}''")
    end
', ' Support function to clear out the slotlink field if
 it still points to specific slot');

