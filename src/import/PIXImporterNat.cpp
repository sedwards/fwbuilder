/*

                          Firewall Builder

                 Copyright (C) 2007 NetCitadel, LLC

  Author:  Vadim Kurland     vadim@fwbuilder.org

  This program is free software which we release under the GNU General Public
  License. You may redistribute and/or modify this program under the terms
  of that license as published by the Free Software Foundation; either
  version 2 of the License, or (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  To get a copy of the GNU General Public License, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

*/

#include "../../config.h"

#include "PIXImporter.h"

#include <ios>
#include <iostream>
#include <algorithm>
#include <memory>

#include "interfaceProperties.h"
#include "interfacePropertiesObjectFactory.h"

#include "fwbuilder/FWObjectDatabase.h"
#include "fwbuilder/AddressRange.h"
#include "fwbuilder/Resources.h"
#include "fwbuilder/Network.h"
#include "fwbuilder/Address.h"
#include "fwbuilder/InetAddr.h"
#include "fwbuilder/IPService.h"
#include "fwbuilder/ICMPService.h"
#include "fwbuilder/TCPService.h"
#include "fwbuilder/UDPService.h"
#include "fwbuilder/NAT.h"
#include "fwbuilder/RuleElement.h"
#include "fwbuilder/Library.h"

#include "../libgui/platforms.h"

#include <QString>
#include <QtDebug>

extern int fwbdebug;

using namespace libfwbuilder;
using namespace std;


QString GlobalPool::toString()
{
    QString l("number %1, interface %2, address range %3-%4, netmask %5 ");
    return l.arg(num).arg(pool_interface.c_str())
        .arg(start.c_str()).arg(end.c_str()).arg(netmask.c_str());
}

string GlobalPool::toStdString()
{
    return toString().toStdString();
}

GlobalPool& GlobalPool::operator=(const GlobalPool &other)
{
    num = other.num;
    pool_interface = other.pool_interface;
    start = other.start;
    end = other.end;
    netmask = other.netmask;
    return *this;
}


void PIXImporter::addGlobalPool()
{
    bool ok = false;
    int n;
    n = QString(tmp_global_pool.str_num.c_str()).toInt(&ok);
    if (ok)
    {
        tmp_global_pool.num = n;
        global_pools[tmp_global_pool.num].push_back(tmp_global_pool);
        addMessageToLog("Global address pool: " + tmp_global_pool.toString());
    }
}

void PIXImporter::pushNATRule()
{
    assert(current_ruleset!=NULL);

    switch (rule_type)
    {
    case NATRule::DNAT:
        buildDNATRule();
        break;

    case NATRule::SNAT:
        buildSNATRule();
        break;

    default:
        assert(rule_type!=NATRule::DNAT && rule_type!=NATRule::SNAT);
    }
}

/*
 * DNAT rule.
 *
 * Using real_a, real_nm, mapped_a, mapped_nm, real_addr_acl,
 * real_port_spec, mapped_port_spec, prenat_interface,
 * postnat_interface
 */
void PIXImporter::buildDNATRule()
{
    addMessageToLog(QString("Destination translation rule (\"static\" command)"));

    newNATRule();

    NATRule *rule = NATRule::cast(current_rule);

    Interface *pre_intf = getInterfaceByLabel(prenat_interface);
    Interface *post_intf = getInterfaceByLabel(postnat_interface);

    rule->setAction(NATRule::Translate);

    if (real_nm.empty()) real_nm = InetAddr::getAllOnes().toString();
    if (mapped_nm.empty()) mapped_nm = InetAddr::getAllOnes().toString();

    if ( ! mapped_a.empty())
    {
        if (mapped_a == "interface")
        {
            RuleElementODst* odst = rule->getODst();
            assert(odst!=NULL);
            odst->addRef(post_intf);
        } else
        {
            dst_a = mapped_a;
            dst_nm = mapped_nm;
            addODst();
        }
    }

    if ( ! real_a.empty())
    {
        dst_a = real_a;
        dst_nm = real_nm;

        RuleElement* tdst = rule->getTDst();
        assert(tdst!=NULL);
        FWObject *s = makeDstObj();
        if (s) tdst->addRef( s );
    }

    if ( ! real_addr_acl.empty())
    {
        UnidirectionalRuleSet *rs = all_rulesets[real_addr_acl];
        if (rs)
        {
            RuleElement* tdst = rule->getTDst();
            assert(tdst!=NULL);

            PolicyRule *policy_rule = PolicyRule::cast(
                rs->ruleset->getFirstByType(PolicyRule::TYPENAME));
            
            if (policy_rule)
            {
                RuleElement *src = policy_rule->getSrc();
                for (FWObject::iterator it=src->begin(); it!=src->end(); ++it)
                {
                    FWObject *o = FWReference::getObject(*it);
                    tdst->addRef(o);
                }
            }

            rs->to_be_deleted = true;
        }
    }

    if ( ! mapped_port_spec.empty())
    {
        src_port_spec = "";
        dst_port_op = "eq";
        dst_port_spec = mapped_port_spec;

        RuleElement* osrv = rule->getOSrv();
        assert(osrv!=NULL);
        FWObject *s = Importer::makeSrvObj();

        if (s) osrv->addRef( s );
    }

    if ( ! real_port_spec.empty())
    {
        src_port_spec = "";
        dst_port_op = "eq";
        dst_port_spec = real_port_spec;

        RuleElement* tsrv = rule->getTSrv();
        assert(tsrv!=NULL);
        FWObject *s = Importer::makeSrvObj();

        if (s) tsrv->addRef( s );
    }

    RuleElement *itf_i_re = rule->getItfInb();
    assert(itf_i_re!=NULL);
    itf_i_re->addRef(post_intf);

    RuleElement *itf_o_re = rule->getItfOutb();
    assert(itf_o_re!=NULL);
    itf_o_re->addRef(pre_intf);

    // add it to the current ruleset
    current_ruleset->ruleset->add(rule);
    addStandardImportComment(rule, QString::fromUtf8(rule_comment.c_str()));
}

/*
 * SNAT rule. Using rule_type, global_pools, prenat_interface,
 * nat_num, nat_a, nat_nm, nat_acl, max_conn, max_emb_conn
 *
 * Note that there can be multiple global pools with the same number
 * and same or different interfaces.  In that case we should create
 * multiple SNAT rules.
 */
void PIXImporter::buildSNATRule()
{
    addMessageToLog(QString("Source translation rule (\"nat\" command)"));

    bool ok = false;
    int pool_num = QString(nat_num.c_str()).toInt(&ok);
    // Parser matches INT_CONST so it can't be anything but integer...
    assert (ok);

    foreach(GlobalPool pool, global_pools[pool_num])
    {
        if (fwbdebug)
        {
            qDebug() << "NAT command num=" << pool_num;
            qDebug() << "nat_a=" << nat_a.c_str()
                     << "nat_nm=" << nat_nm.c_str();
            qDebug() << "Using pool " << pool.toString();
        }

        Interface *post_intf = getInterfaceByLabel(pool.pool_interface);

        newNATRule();

        NATRule *rule = NATRule::cast(current_rule);

        Interface *pre_intf = getInterfaceByLabel(prenat_interface);

        rule->setAction(NATRule::Translate);

        if ( ! nat_a.empty())
        {
            src_a = nat_a;
            src_nm = nat_nm;

            RuleElement* osrc = rule->getOSrc();
            assert(osrc!=NULL);
            FWObject *s = makeSrcObj();
            if (s) osrc->addRef( s );
        }

        if ( ! nat_acl.empty())
        {
            UnidirectionalRuleSet *rs = all_rulesets[nat_acl];
            if (rs)
            {
                RuleElement* osrc = rule->getOSrc();
                assert(osrc!=NULL);

                PolicyRule *policy_rule = PolicyRule::cast(
                    rs->ruleset->getFirstByType(PolicyRule::TYPENAME));
            
                if (policy_rule)
                {
                    RuleElement *src = policy_rule->getSrc();
                    for (FWObject::iterator it=src->begin(); it!=src->end(); ++it)
                    {
                        FWObject *o = FWReference::getObject(*it);
                        osrc->addRef(o);
                    }
                }

                rs->to_be_deleted = true;
            }
        }

        ObjectSignature sig(error_tracker);
        FWObject *addr = NULL;

        if (pool.start == "interface")
        {
            addr = post_intf;
        } else
        {
            if (pool.start == pool.end)
            {
                sig.type_name = Address::TYPENAME;
                sig.address = pool.start.c_str();
                sig.netmask = pool.netmask.c_str();
            } else
            {
                sig.type_name = AddressRange::TYPENAME;
                sig.setAddressRangeStart(pool.start.c_str());
                sig.setAddressRangeEnd(pool.end.c_str());
            }
            addr = address_maker->createObject(sig);
        }

        RuleElement* tsrc = rule->getTSrc();
        assert(tsrc!=NULL);
        if (addr) tsrc->addRef( addr );

        RuleElement *itf_i_re = rule->getItfInb();
        assert(itf_i_re!=NULL);
        itf_i_re->addRef(pre_intf);

        RuleElement *itf_o_re = rule->getItfOutb();
        assert(itf_o_re!=NULL);
        itf_o_re->addRef(post_intf);

        // add it to the current ruleset
        current_ruleset->ruleset->add(rule);
        addStandardImportComment(rule, QString::fromUtf8(rule_comment.c_str()));
    }

}
