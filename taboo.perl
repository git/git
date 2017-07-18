#!/usr/bin/perl -w

my $tmpl = '	if (%%PATTERN%%) {
		print "$lineno ${_}matches %%QPATTERN%%\n";
		return;
	}
';
my $stmt = "";
my $in_header = 1;

while (<DATA>) {
	if (/^\$global_taboo_body =/) {
		$in_header = 0;
	}
	next if (/^\043/ || /^\$/ || /^END$/ || /^\s*$/);
	chomp;
	my $p = $_;
	if ($in_header) {
		$p = '/^[-\w_]*:/ && ' . $p;
	}
	my $q = quotemeta($p);
	my $stmt1 = $tmpl;
	$stmt1 =~ s|%%PATTERN%%|$p|g;
	$stmt1 =~ s|%%QPATTERN%%|$q|g;
	$stmt .= $stmt1;
}
close DATA;

$stmt = 'sub check {
	my ($line, $lineno) = @_;
' . $stmt . '
}
';
eval $stmt;
while (<>) {
	check($_, $.);
}

my $how_to_update_this_script = <<'EOF' ;
	( sed -e '/^__DATA__$/q' taboo.perl && \
	  wget -q -O - http://vger.kernel.org/majordomo-taboos.txt ) \
		>taboo.perl+
	if diff -u taboo.perl taboo.perl+; \
	then \
		rm -f taboo.perl+; \
		echo >&2 No changes.; \
	else \
		mv taboo.perl+ taboo.perl; \
		chmod +x taboo.perl; \
	fi
EOF

__DATA__
#TABOO-START
#
# These are Majordomo's  global  majordomo.cf  as used at
# vger.kernel.org.  This is automated extract from running
# system configuration. THESE MIGHT NOT BE USEFULL IN ANY
# OTHER ENVIRONMENT, AND THIS EXTRACT IS SHOWN ONLY FOR
# YOU TO SEE, WHAT TRIGGERS BLOCKING AT VGER'S LISTS.
#
# taboo headers to catch
#
$global_taboo_headers = <<'END';
m/From:.*MAILER-DAEMON/i
m/^Mailing-List:/i
m/^list-/i
/^subject: ndn: /i
/^subject:\s*RCPT:/i
/^subject:\s*Delivery Confirmation\b/i
/^subject:\s*NON-DELIVERY of:/i
/^subject:.*Undeliverable Message\b/i
/^subject:.*Receipt Confirmation\b/i
/^subject:.*Failed mail\b/i
/^subject:.*Returned mail\b/i
/^subject:\s*unable to deliver mail\b/i
/^subject:\s.*\baway from my mail\b/i
/^subject:\s*Autoreply/i
/#field0#/
m%content-type:.*text/html%i
/x-mailing-list:.*\@vger\.kernel\.org/i
# DATE:   25 Jun 01 3:08:39 AM
m/DATE:\s*..\s...\s..\s.*:..:..\s..\s*$/i
m/nntp-server.caltech.edu/
m/Mail Bomber/
m/X-Mailman-Version:/
m/X-EM-Registration:/
m/x-esmtp:/
m/Local time zone must be set/
m/X-Mailer:.*eMerge/i
m/X-Mailer:.Trade-Navigator/i
m/From:.*MAILER-DAEMON/i
m/X-Mailer:.*Group Mail/
m/^Status:/
m/^X-Status:/
m/X-Set:/
m/^X-Mailer:.*JiXing/
m/^X-Mailer:.*MailXSender/
m!Message-Id:.*<.*\@vger.kernel.org>!
m!Message-Id:.*<.*\@zeus.kernel.org>!
m/Subject:.*detected a virus /
#m/Anti-Virus/i
m/Subject:.*[Vv]irus [Ff]ound/
m/Subject:.*[Vv]irus [Aa]lert/
m/^Subject:\s*Report to Sender/
m/^Subject:.*AntiVir ALARM/
m!^X-Library:\s*Indy!
m!Content-Type:\s*application/x-msdownload!
m!Conetnt-Type:\s*application/msword!
m!MiME-!
m!netdev-bounce\@oss\.sgi\.com!
m!Undeliverable:!
m!Syntax error in!
m!^Illegal-Object:!
m!Subject:.*paycheck!i
m!Subject:.*Urgent\s*Business\s*Request!i
m!Subject:.*Urgent\s*Business!i
m!Subject:.*Business\s*Request!i
m!Subject:.*Mail delivery failure!
m!Subject.*\[SPAM\]!
m!desease!
m!Subject:.*Norton AntiVirus detected!
m!X-Spam-Flag:.*YES!
m!Subject:.*\sSARS\s!i
m!Subject:.*MMS Notification!
m!Subject:.*Rejected Mail!
m!Subject:.*Report to Recipient!
m!Subject:.*You sent potentially!
m!Subject:.*penis.*!i
m!WAVA Postmaster!
m!^SUBJECT:!
m!Delivered-To:!
m!Subject:.*Footprints!
m!^From:\s*Majordomo!
m!Subject:\s+Out of Office AutoReply!
m!Content-Type: multipart/alternative!
m!From:.*amavisd-new!
m!Subject:.*found.*virus!i
m!Subject:.*As Seen on CNN!i
m!Subject:.*Mail Delivery!
m!Subject:.*Essential.*Software.*On.*CD!i
m!LIPITOR!
m!VIOXX!
m!XANAX!
m!CELEBREX!
m!PROZAC!
m!VALIUM!
m!seen it on TV!
m!Valium!
m!Prozac!
m!ScanMail for Lotus Notes!
m!InternetBank Agreement!
m!X-WEBC-Mail-From-Script:!
m!X-Mailer: RLSP Mailer!
m!Subject: Rediff\'s Auto Response!
m!Email account utilization warning!
m!From:.*Lyris.List!
m!Listar command results!
m!EHLO vger.kernel.org!
m!HELO vger.kernel.org!
m!Subject:\s*BOUNCE !
m!Subject:.*Rolex!i
m!CeBIT!
m!Message Blocked!i
m!Subject:.*Diploma!i
m!stk-sub!
m!owner-majordomo\@!
END

# TABOO BODY
#
# Taboo body contents to catch and forward to the approval address
#
# For example:
#   $global_taboo_body = <<'END';
#   /taboo topic/i
#   /another taboo/i
#   END
# NOTE! Using ' instead of " in the next line is VERY IMPORTANT!!!
#
$global_taboo_body = <<'END';
m!^X-Mailing-List: !o
m!^List-ID: !o
m%Content-Type:.*text/html%io
m%Content-Type:.*multipart/alternative%io
m/adult site/io
m/Cyber FirePower/io
m/TO BE REMOVED /o
m/cannot be considered spam/io
m/can not be considered spam/io
m/can not be considered as spam/io
m/To be removed from this list/io
m/THIS EMAIL COMPLIES WITH ALL REGULATIONS AND IS NOT SPAM/io
m/Congress shall make no law respecting an establishment of religion/io
m/This is one time .*mail/io
m/This is not a Spam/io
m/This is not spam/io
m/web hosting international/io
m%murkowski/commercialemail%io
m%SECTION 301%io
#m/charset=.*windows-/io
m/Bill s\.1618 TITLE III/io
m/Please no mail bombs, legit removal/io
m/We apologize if this message has reached/io
m/Messaggio promozionale/io
m/name=.*\.vbs/io
m/name=.*\.scr/io
m/If you are receiving this by mistake/io
m/Serious Inquiries Only/io
m/Serious Enqueries Only/io
m/FOLLOWING IS A NOTE FROM THE ORIGINATOR OF THIS PROGRAM/io
m/AS SEEN ON NATIONAL TV/io
m/CDmail by ClanSoft/io
m/DO NOT SPAM\. IT HURTS ALL OF US/io
m/LASER PRINTER SUPPLIES/io
m/If this message has reached you in error/io
m/\@minister\.com/io
m/SENDING BULK E-MAIL LEGALLY/io
m/Advertising for Free/io
m/PLACING FREE ADS ON THE INTERNET/io
m/The Insider\'s Guide to/io
m/Address Removal Instructions/io
m/Advertise via Email/io
m/mailing is done by an independent marketing/io
m/DO NOT REPLY TO E-MAIL/io
m/This message was sent using an evaluation copy of/io
m/Promozione/io
m/Promozionale/io
m/use fax order form/io
m/with Remove in the subject/io
m/to be excluded from further communication/o
m/Hola como estas/o
m/Hi! How are you/o
m/business letter from Beijing/io
m/LASER PRINTER TONER/io
m!TONER SUPPLIES!o
m/dotsex.com/io
m/dotsex.net/io
m/Currency Trading/io
m/jinfengnet/io
m/bulk email network/io
m/SEX-SERVER/io
m/^Content.+name=.+\.pif/o
m/Unive rsal Adve rtising Syste ms/io
m/result of your feedback form/io
m{http://www\.copydvd\.net}io
m/LAGOS.*NIGERIA/io
m/zimbabwe/io
m/petroleum/io
m/petroluem/io
m/APOLOGISE IF YOU HAVE ALREADY RECEIVED THIS E-MAIL/io
m/AaZbooks/o
m/V I R U S   A L E R T/o
m/ScanMail for Microsoft Exchange/o
m/www\.myparty\.yahoo\.com/o
m/E-mmunity has detected/o
m/netfirms\.com/io
m%http://nse.yam.com/%o
m/responda este com o assunto REMOVER/io
m/Powered by List Builder/o
m/If you do not wish to receive similar emails/o
m/LuTong/io
m/RemoteListSummary/io
m/freewebhost4all/io
m/Are you tired of getting up early/io
m/credit card needed/io
m/CustomOffers.com/io
m/Bu maili bir daha/io
m/mail adresine bos/io
m/To be opt out /io
m/ business relationship /io
m/forward looking statements/io
m/forward-looking statement/io
m/WWW.OSIOS.ORG/o
m/As seen on NBC, CBS, CNN, and even Oprah/io
m/holmecomputing/io
m/to you as a surprise/o
m/RAV AntiVirus/o
m!Trend Micro Anti-Spam !o
m!Online Journaled File System!o
m/Jesus Christ/o
m/promotional mail/o
m/This is NOT spam/o
m/lcdmodule\@/o
m/<html>/o
m/<HTML>/o
m/Cashfiesta.com/o
m/BUSINESS RELATIONSHIP/o
m/unsubscribe from any mail lists/o
m/STOCK SET TO EXPLODE/io
m/OTCBB/o
m!^Current Price!o
m!^Stock:!o
m/have received this message in error/o
m/SESE-SEKO/o
m/ANAND BHATT/io
m/modalities/io
m/free adult site/io
m/mobutu/io
m/livechat/io
m/softcore/io
m/nigeria[. ]/io
m/liberia/io
m/ghana[. ]/o
m/special mailing/io
m/STRICTLY CONFIDENTIAL/io
m/HIGHLY CONFIDENTIAL/io
m/CONFIDENTIAL/o
m/URGENT BUSINESS/io
m/utmost secrecy/io
m/lovecity.ru/io
m/cool-date.net/io
m/5863 Leslie St/o
m!cgi-bin/varpro!o
m!EXCITING OFFER!io
m!clubdepot!io
m!ragazze!io
m!indebted for your address!o
m!business proposal!io
m!probate!io
m!beesold.com!io
m!which contained the viruses!o
m!sales\@!o
m!mailcomesandgoes!io
#m![\200-\237\241-\377]{8}!o
m!^Content-Type: application/ms-tnef!o
m!fastbizonline!o
m/INTERESSADO ACREDITE/io
m/TOTALMENTE GRATIS/io
m/TECAVUZ/o
m/KIZLAR/o
m/KIZLARI/o
m/SAPIK/o
m/HATUN/o
m/GIZLI KAMERA/o
m!PAYCHECK!o
m!MONEY MAKING OPPORTUNITY!io
m!NeuroTherapy!o
m!Below is the result of your form!o
m!Below is the result of your feedback form!o
m!Pheromone!io
m!Barrister!io
m/risk free transaction/io
m/smokesdirect/io
m/China Enterprise Management/io
m/unido.chinatop.net/io
m/Rus-SexServer/o
m!lol.to/bbs!o
m!Sales-O-Matic!io
m!INVESTMENT!o
m!INTRODUTION!o
m!To grab your FREE!io
m!DHS Club !o
m!Secure your position in!io
m!^X-Library:\s*Indy!o
m!singles-contacts!o
m!sincerely apologise if this e-mail!o
m!^Content-Type:\s*application/x-msdownload!o
m!^Content-Type:\s*application/msword!o
m!Trial Version of WorldMerge!o
m!artofservice!o
m!However strange or surprising this contact!o
m!business relationship with you!o
m!templatestyles\.com!o
m!THIS IS A ONE TIME SUBMISSION!io
m!YOUR NAME WAS SELECTED!o
m!This email is sent in compliance with strict anti-abuse!io
m!REVISTAS ERÓTICAS!o
m!Erotic magazines!o
m!Infosource Group!o
m!S. 1618 TITLE III!io
m!host4mail!io
m!Assine UOL!o
m!name of Allah!o
m!singles.com!o
m!liquid2d!io
m!To be removed from any future mailings!o
m!qzsoft_directmail!o
m!discountshaven!o
m!Mensaje automático del sistema!o
m!\tname=".*\.pif"!o
m!\tname=".*\.PIF"!o
m!Sign up for your own PayPal account!o
m!Si usted quiere ser removido de nuetra lista envie!io
m!CSmtpMsgPart123!o
m!Lipotropic!o
m!power diet plus!io
m!kabila!io
m!MARYAM!io
m!ABACHA!io
m!Employment Opportunity!io
m!self motivated people!io
m!real world opportunity!io
m!EARN EXTRA INCOME!io
m!Secret to Multilevel marketing!io
m!overseas account!io
m!reliable and honest!io
m!Auditor General!io
m!binding agreement!io
m!/take-me-out/!o
m!This extraordinary offer!io
m!offer ends!io
m!to be removed from our email list!io
m!EMAIL EXTRACTION!io
m!KATHMANDU!io
m!/remove-all/!io
m!Marijuana Alter!io
m!xxxcorner!io
m!Jesus Christ!io
m!buyer.s club!io
m!MINISTRY OF!io
m!OTB Computers!io
m!No sponsoring required!io
m!A ton of helpful Information products!io
m!No selling!io
m!Couple of weeks old!io
m!Pays daily!io
m!One time payment of!io
m!can pay you up to!io
m!Subscription Confirmation!io
m!movieglobe!io
m!savimbi!io
m!political asylum seekers!io
m!political asylum!io
m!asylum seeker!io
m!SEXO SAGRAD!io
m!sexosagrado!io
m!Decreto S\.1618!io
m!/optout/!io
m!dragonmail!io
m!disikus!io
m!^Enough\.!o
m! make money with !io
m!Opt-Out!io
m!Email Marketing!io
m!targetted emails!io
m!MoreInfoOffShore!io
m!If you are a smoker!io
m!lenders compete!io
m!reainance!io
m!Finance Company!io
m!hoop-buy!o
m!hoop-bid!o
m!helpu-web!o
m!annuities!o
m!Vjagrra!io
m!vjaagra!io
m!viagra!io
m!cialis[^a-z]!io
m!twobuswinesdays.com!io
m!braceletnewatch.com!io
m!garment!io
m!UNIVERSITY DIPLOMA!io
m!impeached!io
m!god fearing!io
m!promo code!io
m!lead list!io
m!referral network!io
m!lead list!io
m!UK lotto!io
m!excuria.com!io
m!Visit us on the web!io
m!cyberread!io
m!ebookstand!io
m!\sSARS\s!io
m!Customer Relations Department!o
m!get your essential copy!io
m!protect your privacy now!io
m!camfriend!io
m!webcam commun!io
m!category . winner!io
m!mortgage!io
m!International Promotion!io
m!Cable Descrambler!io
m!internet-offer!io
m!Mini-Breathalyzer!io
m!If you no longer wish to receive our offers and updates!o
m!address attached to ticket number!o
m! Lottery!io
m!captain69!o
m!re-analysing Heisenberg!io
m!Start der SpamAssassin Auswertung!o
m!Please see the attached zip file for details!o
m!\.remova-me\.!o
m!Our virus scanner detected a virus!o
m!Empresa procura!o
m!Empreendedor!o
m!=TELPHONE JOKES=!o
m!To be excluded from future promotions!o
m!SmileAtYou!o
m!Word-of-Mouth!o
m!mortage!io
m!the banks know about!io
m!lower mortgage repayments!io
m!softwaresavings!o
m!/cgi/redir!o
m!http://btrack.iwon.com/r\.pl\?redir=!o
m!best deal on your!o
m!VIRUS DETECTED!o
m!VIRUS ALERT!o
m!TROJAN DETECTED!o
m!DID YOU KNOW|A HACKER COULD BE!o
m!\e\[B!o
m!onlinesaleew!o
m!profitableproduct!o
m!slashmonthlypayments!o
m!dont want any more!o
m!dont want me to write any more!o
m!Combine your debt into !o
m!mail15.com!o
m!See the attached file for details!o
m!Please see the attached file for details!o
m!V I R U S  A L E R T!o
m! VIRUS NOTIFICATION !o
m!%RANDOM_CHAR!o
m!Sobiga.F!o
m!Sobig\.f!o
m!interest on your debt!o
m!use this Internet Explorer patch now!o
m!UnbelievableSecretsCd!o
m! to you as a surprise!o
m!Your cash advance !o
m!Get your advance today!o
m!gallery-a.ru!o
m!1automationwiz!o
m!A Gift Of Poetry!o
m!\sSecurity\s+Company!io
m!THE CHILDREN OF GOD!io
m!born again Christian!io
m!v[i1]c[o0]d[i1]n!io
m!v[i1]cidon!io
m!Levitra!io
m!Blind Date!io
m!Due to mix up of some numbers and names!io
m! found a new typ of worm!o
m!I.ve send you a recover tool, to fix this problem!o
m! Strivectin!o
m!Klein Becker!o
m!lookingforablinddate!o
m!looking-for-you.org!io
m!smartphonessmart.com!io
m!bubbleenveloppe.com!io
m! Branded Watches !io
m!contact4you\.cc!io
m!HalfPriceLotion.com!io
m!Compliments of the day!io
m!mutual benefit!io
m!fiduciary!io
m!Academic Qualifications!io
m!prestigious NON.ACCREDIT+ED universit!io
m!Teledeteccion!io
m! ADMINISTRATION TRAINING !o
m!lolslideshow.com!o
m!Microsale!o
m! 123FreeTravel !o
m!HGH !o
m! Pharmacy !io
m!Xanax!o
m!Valium!o
m!Mlcrosoft !o
m!AmericanGreetings!o
m!If you are a smoker !o
m!parastatal!io
m!beneficiary!io
m!Bank Account!io
m!If you wish to be removed from this mailing list,!io
m!sent this to you by mistake!o
m!dumpsmarket!o
m!with cvv2 information!o
m!message contains Unicode characters and has been sent as!o
m!The message cannot be represented in 7-bit ASCII encoding!o
m!Mail transaction failed. Partial message is available.!o
m!www.rxeasymeds!o
m!CANADA BOOKS!oi
m!visiongain!o
m!This is a machine-generated message, please do not reply via email!o
m!BATES ALAN!o
m!eSafe detected a hostile!o
m!drlaurent.com!o
m!cartmed.com!o
m!medsfactory!o
m!Content-Type: application/x-zip-compressed; name=!o
m!Avtech Direct!o
m!YesPayment!o
m!O e-mail abaixo foi descartado !o
m!7d8NCOsEajX/1yIxiUXuGlAnm3v7Pvj/C/B1FxQrVCVbcG1rKlwAARgnAgHt2yFbKVgQJmr9!o
m!drop the hammer on the next girl!o
m!prescribedmeds.com!o
m!mdrecommends.com!o
m!medspro.com!o
m!medspro.net!o
m!healthpolicy.com!o
m!healthdo.com!o
m!improvedpills!o
m!newmedformula.com!o
m!/sv/index.php!o
m!C1AL1S!o
m!THE UNCERTAINTY PRIN!o
m!MatrixOne Tech Support!o
m!NeVeR!o
m!Content-Type: application/octet-stream; name=".*\.zip"!o
m!new drug that puts!io
m!drugsbusiness!o
m!V i a g r a!o
m!/sv/applepie!o
m!/s95/index.php!o
m!American Medical Directory!o
m!bizdeliver!o
m!chick you screw!io
m!mardox.com/!o
m!Eliminate All Bills!io
m!/gp/default.asp!o
m!lowerrates4you!io
m!ZhongHengLong!o
m!upfeeling\.cn!o
m!upfeeling\.com!o
m!amercenterpub!o
m!/knowspam.net/!o
m!choicerxsource!o
m!virtualcasinoes!o
m!casino-4-free.com!o
m!\@mailhec\.com!o
m!After the age of twenty-one!o
m!/hgh/index!o
m!affiliate_id=!o
m!/affiliate!o
m!/lv/index.php!o
m!/nomoremail!o
m!/gen_ads/!o
m!/pr/index.php!o
m!/gv/index.php!o
m!MEI LUNG HANDICRAFTS!io
m!einnews.com!o
m!deutsches-panel.com!o
m!cablefilterz!o
m! Barrow Linux kernel developer!o
m!TO BECOME A MEMBER OF THE GROUP!o
m!/rd.yahoo.com/!o
m!Canadian Subsidies !io
m!Canadian Business !io
m!C a n a d i a n!io
m!Associate Degree!io
m!Nutritionist!io
m!l0se weight!io
m!H[o0][o0]d[i1l]a!io
m!advicefound!io
m!bestsevendiamonds!io
m!/B2B/!io
m!fastherb.biz!io
m!.tealis.com!io
m!message contained restricted attachment!o
m!casinoes-4-you!io
m!urban-casino!io
m!this is an automated reply!io
m!this is an automatic reply!io
m!this is automatic reply!io
m!/av/val/!o
m!buycheapdrugs.biz!o
m! DEALS on SOFTWARE !io
m!teddychoice.biz/!o
m!peopleloveit.biz!o
m!shyx.biz/!o
m!shyxp.biz/!o
m!shyx.us/!o
m!shyxp.us/!o
m!sharyx.us/!o
m!loveforlust.biz!o
m!>Zitat:<!io
m!>Zitat<!io
m!NoTurkishmembership.com!io
m!Euronational.org!io
m!auslaendergewalt.ch!io
m!der-ruf-nach-freiheit.de!io
m!Lese selbst:!io
m!Beitritts!io
m!Bullet Proof your Web Site!io
m! Fax Broadcasting !io
m!marketing tool!io
m!Major credit card!io
m!National Library of Canada!io
m!diet40.com/!io
m! Our low Price: !o
m!CAN[- ]SPAM Act of 2003!io
m!Ruecksichtslos|Polizeihunden!io
m!privacykeeper.info/!o
m!Curso GIS y Teledetec!io
m!kfjlfjka.biz/!io
m!lddekfan.info/!io
m!jjglcllj.info/!io
m!nfkiijl.info/!io
m!Frank Ike!io
m!shareye.us/!io
m!shareye.biz/!io
m!^Email-Verify-Code: !o
m!bioessence.com/!io
m!Adipren!io
m!eurofreelancers!io
m!EUROMAIL LOTTO INTERNATIONAL!io
m!cutpricerxpills.com!io
m!tien-huang.com!io
m!gs-us.biz/!io
m!klcbhgf.biz/!io
m!bhgncge.info/!io
m!casino\.biz!io
m!bigbonus-casino!io
m!Windows XP Pro!o
m!Norton Antivirus!o
m!Symantec WinFax!o
m!cialis-is-better!io
m!Caiilis!io
m!benedicta!io
m!message was not delivered due to the following reason!o
m!The following addresses had permanent fatal error!o
m!Dear user of vger.kernel.org,!o
m!Your account was used to send a large amount of!o
m!Your account has been used to send a huge amount of!o
m!account has been used to send a large amount of!o
m!^\tfilename=".*\.zip"!o
m!^\tfilename=".*\.ZIP"!o
m!^\tfilename=".*\.exe"!o
m!^\tfilename=".*\.EXE"!o
m!^\tfilename=".*\.bat"!o
m!^\tfilename=".*\.BAT"!o
m!^\tfilename=".*\.com"!o
m!^\tfilename=".*\.COM"!o
m!; filename=".*\.zip"!o
m!; filename=".*\.ZIP"!o
m!; filename=".*\.exe"!o
m!; filename=".*\.EXE"!o
m!; filename=".*\.bat"!o
m!; filename=".*\.BAT"!o
m!; filename=".*\.com"!o
m!; filename=".*\.COM"!o
m!^\tname=".*\.zip"!o
m!^\tname=".*\.ZIP"!o
m!^\tname=".*\.exe"!o
m!^\tname=".*\.EXE"!o
m!^\tname=".*\.bat"!o
m!^\tname=".*\.BAT"!o
m!^\tname=".*\.com"!o
m!^\tname=".*\.COM"!o
m!Network Associates WebShield !o
m!Shengli Oilfield!o
m!go-l\.com!o
m!cracksoftbuy.com!o
m! CheapSoft !o
m! Chongqing !o
m!dfjndfv.com/!o
m!LIPITOR!o
m!VIOXX!o
m!XANAX!o
m!CELEBREX!o
m!PROZAC!o
m!VALIUM!o
m!Canadian Pharmacy!o
m!we49fm.com/!o
m!hchcgem.info/!o
m!Erectile!o
m!Cailis!io
m!Caiilis!io
m!cheapergenerics.com!o
m!esophageal cancer!io
m!ijjad.com!io
m!enhancemefast7.com!o
m!baba csoportnak!o
m!Hongming Foundation!o
m!hongming.us/!o
m!TurboTax!o
m!e-wowza.com!o
m!aamedical.net!o
m!biblerevelations.org!o
m!gogo-soft.info!o
m!emlakarabul!io
m!siqop.com!o
m!money making phenomenon!io
m!plasma-connection.com!o
m!Committee of China!o
m!bestfranchiseopportunities!o
m!baba.lx.hu!o
m! Q E M !o
m! Q E N !o
m!You will receive offers from!o
m!cpppo2.com/!o
m!On medication long term!o
m!GREAT SPECIALS!o
m!hjigoeoi.com/!o
m!No doctor visits!o
m!avtechdirectcomputers!o
m!CIAL1S!o
m!CIAzLIS!io
m!fastveryfast.com!io
m!^Last Trade:!io
m!^Day High:!io
m!ejaculation!io
m!erectile!io
m!searching for representatives!o
m!HUAMAO ARTS AND CRAFTS!o
m!Target Email!o
m!share Opinions and Experiences!io
m!Windows 2000 Pro!o
m!selectiveproductsite!o
m!uniqueinvestproducts!o
m!frank-cd-review!o
m!get-it-online.info!o
m!Presidential platform!o
m!lowcostgenerics.com!o
m!wellnessone!o
m!monsterprelaunch!o
m!www.milestechnologies.com!o
m!Fadi Basem!o
m!discountedmeds.net!o
m!pillswholesale!o
m!rxrocks.com!o
m!Alpen-Antique!o
m!alpenantique!o
m!titanium dioxide chemical!o
m!You are already +a *p *p *r *o *v *e *d!o
m!the-rxsite.com!o
m!yahoohut.com!o
m!werkinformatie.nl/cards!o
m!theseto\.com/!o
m!fta-canada\.net!o
m!\.procourtmi\.com/!o
m!superchapultram\.com!o
m!\.psellismbj\.com/!o
m!\.somamegastore\.com!o
m!\.misstip\.com/!o
m!\.subduingdi\.com/!o
m!\.deprivalai\.com!o
m!\.beshrouddm\.com!o
m!Visa Seal of Confidence!io
m!bhex.com/!o
m!rogernutt.com!o
m!You live in a foreign land far away from mine!o
m!Domicilliary!io
m! next of kin !io
m!Please verify your information!io
m!Your application was processed!io
m!.midord\.com!io
m!.idnto\.com!io
m!.gtxtr\.com!io
m!downloadsupercenter.com!io
m!Rolex!io
m!\.seaweedyhf\.com/!io
m!WINNING NOTIFICATION!io
m!FusionPHP.NET!o
m!sales manager!io
m!L e v i t r a|C i a l i s!io
m!aurilavepoppetleggumdigging!o
m!snapped4stockyard!o
m!unloaded2katzner!o
m!Globalzon Consulting!o
m!GGLC,GGLC:1969-53!o
m!productsuperpacks!o
m!jrz874383w.com/cs!io
m!TheArtHaven.com!io
m!we-private.com!o
m!bestlenderz.com!io
m! low interest mort!io
m!rateznow.com!io
m!sssexplicit.com!o
m!.m0rtgagesource.com!o
m!new home loan!io
m!123ratezz.com!io
m!7913658094!o
m!lendez.com!o
m!RusDeluxe!io
#m!Ba\(d!io
m!alivelybaby.com!o
m!warmlighthouse.com!o
m!try-logos-!o
m!try-logosxd!o
m!trylogos!o
m!marklogo-ax!o
m!mark-logo!o
m!new-medz.com!o
m!innerlogos.com!o
m!CHINA DONG FENG MOTOR!o
m!Esophage!io
m!ganz-privat!o
m!easy-lenders.com!o
m!future IPSI BgD!o
m!Chief Auditor!o
m!solidbay.com/!o
m!BestPrizesOnline!io
m!productivenets!io
m!EuroTransfer!o
m!sms.ac/!io
m!Documentaci.n necesaria!o
m!Khodorkovsky!io
m!Dear e-gold member!io
m!have one dollar for!io
m!/deletion.asp!o
m!/formupdate.asp!o
m!Clinically Tested!o
m!whuzgdaid.com!o
m!tritylbcgbj.com!o
m!emncnsjf.com!o
m!ogagmsoga1.com!o
m!wisnjwis7.com!o
m!arofcaro7.com!o
m!jujkju1.com!o
m!rumnkrum4.com!o
m!SGETEK!o
m!iforyou.org!o
m!get-laid-easy.com!o
m!onlyliveonce!o
m!get-some-action!o
m!floats your boat!io
m!pluralizerhe\.info!o
m!subreptiongf\.info!o
m!\.forgerma\.com!o
m!bestpills4all.info!o
m!Italian Travel Agency!o
m!\.pedaryil\.com!o
m!unpardoned.net/cs/!o
m!OTC Bulletin Board!o
m!OTC:!o
m!go2l\.info!o
m!MENTORN\.TV!io
m!ox-files\.info!o
m!lokingforaman\.org!o
m!Bad credit!io
m!We deliver medication !io
m!\.defeasemc\.com!o
m!\.mitigatorji\.com!o
m!\.pearlylj\.net!o
m!\.zorromf\.info!o
m!\.absentmentjh\.biz!o
m!loseweightsystems!o
m!partied.net/cs/!o
m! custom logos !io
m!Penis Enlargement!io
m!gretan\.com/ss!o
m!\.episcopemk\.info!o
m!\.maomaolf\.info!o
m!\.rametjd\.info!o
m!\.semestraldj\.info!o
m!advicemortgage\.net!o
m!m0untains\.net!o
m!buychepmeds.com!o
m!siratu\.com!o
m!\.paradermdi\.info!o
m!\.carucatedlf\.info!o
m!\.canellaih\.info!o
m!\.LeaveForWhat\.net!o
m!Spyware Stormer!o
m!\.br1ght\.com/!o
m!\.hatrailna\.info!o
m!sofastloads.com!o
m!\.socagefh\.com!o
m!\.misbornai\.com!o
m!\.bannerlikedj\.com!o
m!\.wadlikeei\.com!o
m!\.spiceableck\.biz!o
m!\.tussurke\.net!o
m!\.romanej\.com!o
m!\.choanosomeab\.com!o
m!\.agnosislk\.com!o
m!\.kaiserismcd\.com!o
m!\.isothujonene.org!o
m!\.trophemahi\.info!o
m!\.lhotacg\.com!o
m!Permettez-moi de vous!o
m!administration of vger.kernel.org would like to let you know the following!o
m!LASER DENTAL DE GUATEMALA!o
m!This is an automatic email!o
m!please do not reply to this email!o
m!AntiVir has scanned a mail!o
m!email account was used to send!o
m!e-mail account has been used to!o
m!Sao Tome and Principe!o
m!paid4sign!o
m!sign_ups!o
m!I will mentor you,!o
m!\@isp-q\.com!o
m!SPUR-M!o
m!Okul Oncesi!o
m! need cooperation with you!o
m!ligenitaljh.com!o
m!bestkingplace.net!o
m!freeadguru.com!o
m!PerHits.de!io
m!Russian Marriage Agency!io
m!looking for business!io
m!e-BookServices!io
m!expertwitness!io
m!ephedra!io
m!ultramegasuper-site!io
m!bestkingplace.net!io
m!watch3znowbymai15l!io
m!STRONG BUY!io
m!verimer-australia!io
m!Bulk BP Host!io
m! Adult Dating !io
m!We very sorry If you receive our email !io
m!greenones.com!io
m! THERUSMARKET !io
m! Replika !io
m!enewstoday20livemail.com!io
m!encrypted-inquiry\.cn!io
m!bookold\.com!io
m!PRlCE !io
m!vetok\.com!io
m!.wootop\.com/!io
m!ftime\.net!io
m!perscription!io
m!for your meds!io
m! quality meds !io
m!monthlysearch\.com!io
m!Suppress your appetite!io
m!silverdiet.com!io
m!global-transfer-form!io
m!Global Cash!io
m!mysiteherenow.com!io
m!newsletter\@investorguide!io
m!\.oemdg\.com!io
m!armrestnk\.com!io
m!CHNW!o
m!Cash Now Corporation!io
m!whatisthafuture\.com!io
m!pistachiopack.com!io
m!digintothis.com!io
m!eltacnet.net!io
m!midwindow.net!io
m!Applied Market Analytics!io
m!Informati0n!io
m!st4tement!io
m!they-love-much.net!io
m!its-finally-here.com!io
m!onlinesuperday\.com!io
m!firstcentre\.com!io
m!whitlingnn\.com!io
m!chinchge\.com!io
m!openmoment\.com!io
m!endymalj\.com!io
m!Jack Rabbit !io
m!fyponynucakye.com!io
m!multicountry.com!io
m!pixelsthatrock.com!io
m!howtobeasmartshopper.com!io
m!neckthu.com!io
m!refinance!io
m!seesproof\.com!io
m!taikang\.com!io
m!reportratings.com!io
m!linelinks.net!io
m!sexual partner!io
m!Mature Babe!io
m!oureasyshopping.com!io
m!jelh\.com!io
m!h1gher\.net!io
m!cortexmc\.com!io
m!futuremakes\.com!io
m!greatestmessenger\.com!io
m!Khordokovsky!io
m!Cute Babe!io
m!Bl[o0]wj[o0]b!io
m!doetave.com!io
m!\.mfek\.com!io
m!\.ppeq\.com!io
m!What IS 0EM software!io
m!hookup-now\.net!io
m!donaryfl\.com!io
m!Milutinovic!io
m!hornypillzz.net!io
m!Online Ph.rm.cy !io
m!^Good Trading!o
m!promorning.info/!io
m!spermamax!io
m!realquikx.com!io
m!communizding\.com!io
m!judgmentprocessing\.com!io
m!colowever\.com!io
m!reverendmm.com!io
m!^Symb0l:!io
m!^Symbol:!io
m!^Current Price!io
m!yourforless.com!io
m!penknifepress\.!io
m!hottubslopping\.com!io
m!l1fed33r\.com!io
m!citybestonline\.com!io
m!Ph\?rm\@cy!io
m!Voagra!io
m!Search Engine Optimization!io
m!1stinline\.info!io
m!This stock!io
m!parklife-crm.com!io
m! VisNetic MailScan !io
m!\.mergencemh\.com/!io
m!\.kooldowns\.!io
m!real-meds.com!io
m!greatadvance\.com/!io
m!waiting-for-you.org!io
m!im-waiting-4you!io
m!online-casino!io
m!casino-tribune!io
m!casino-focus!io
m!H o l l y w o o d!io
m!\.afcrx\.com!io
m!\.kpiv\.com!io
m!schoolgirl!io
m!uwriteme\.info!io
m!Internet Advertising Agency!io
m!4 Web Marketing!io
m!nicerealmail.info!io
m!timesmooneytoo.com!io
m!watchingtondc.com!io
m!hoodia!io
m!hooddia!io
m!VERIFIED BY BBB!io
m!APPROVED BY VISA!io
m!bedroom!io
m!men enhancement!io
m!virile!io
m!accomplished lover!io
m!expartriate!io
m!aliconferences.com!io
m!join then but I am ready!o
m!Get a diploma !o
m!psa4u-inc.com!io
m!pleased to inform you!io
m!finance.yahoo.com!io
m!FastLength PRO!io
m!theswalo.com!io
m!best sex toy!io
m!comgraviti.com!io
m!my-new-profile.com!io
m!st6y.net!io
m!rallyediiet.com!io
m!cagmon.com!io
m!\@ExlDisc.com!io
m!qandadiet.com!io
m!ftaspecial!io
m!inexpensiveformula.com!io
m!haibinjia\@!io
m!denticledm!io
m!dietdvdcheck!io
m!find-the-right-one.net!io
m!High School Diploma!io
m!Obtain degrees from!io
m!based on you life experience!io
m!hothopkins.com!io
m!amniotemk.com!io
m!484.693.8861!io
m!Your Full name!io
m!Your Mailing address!io
m!Dear Home Owner!io
m!Dear Homeowner!io
m!selectreplics!io
m!fasts-hopping!io
m!xrepliqawatch!io
m!timexsollution!io
m!eastme.com!io
m!faxou.com!io
m!quutme.com!io
m!logotip-marke!io
m!eedown.com!io
m!truereplikas.com!io
m!signalwithredone.com!io
m!lisaquinton.info!io
m!zachemtojerrv!io
m!R_+E_+M_+[0O]_+!o
m!pianosongz.com!io
m!testdatagenerator.info!io
m!poluchidemlopas.com!io
m!dipylonli.com!io
m!faxou.com!io
m!fresh-mobile-content!io
m!KYDEI !io
m!PANSATBOX.INFO!io
m!CHINA GOLD!io
m!nutsforfree.com!io
m!gercekter!io
m!FDA approved!io
m!unertheat\.com!io
m!foigbil\.com!io
m!\$888!io
m!USD 888!io
m!G D K I!o
m!Rec0mmendati0n!io
m!im-waiting-4you!io
m!web-+design-+uk!io
m!web--design-uk!io
m!uk-+web-+design!io
m!whosaysiceannskate!io
m!MM Group Handling!io
m!djjqo8.com!io
m!Sheik Mohammed!io
m!Hoodia!io
m!netequalzier.com!io
m!pipepoints.com!io
m!E'Sellers Financial Group!io
m!ICQ.*277705564!o
m!nfqjqk.com/!io
m!hoopcc.com!io
m! DISC OUNT !io
m!oem2006.biz!io
m!brandedsource!io
m!STAATS LOTERIJ INTERNA!io
m!timexsollution!io
m!leaveitalonefollow!io
m!deedlsonwheelss!io
m!diomedeacb.com!io
m! TESORO !io
m!totallynewprices!io
m!gowwor.com!io
m!unpraiselk.com!io
m!soupofdayys!io
m!animalprot!io
m!extremeci.com!io
m!big-pill.com!io
m!teampills.com!io
m!Wylos!io
m!adslcam.net!io
m!didysir.com!io
m!C G D C!o
m!companycompshare!io
m!fidelityinternational!io
m!fidelity investment!io
m!Duoala Mbale!io
m!paliokertunga.com!io
m!Visit The Site Now!io
m!netsrate.com!io
m!justremedy.com!io
m!i-am-waiting4love!io
m!Woodworking!io
m!myegold!io
m!EgoldStore!io
m!discountofthecenturies!io
m!courtneyishealth!io
m!your e-mail address has been!io
m!pleasegimmie!io
m!mgrecruitment!io
m!stripsofhealthyy!io
m!coderscity!io
m!progcompetitions!io
m!pixelitas!io
m!newgunforsalejoke!io
m!blessthathomeplease!io
m!blue pill!io
m!erection!io
m!l-f-union.net!io
m!support-tolliscatalogue.com!io
m!funfundating.com!io
m!like a pornstar!io
m!male enlargement!io
m!www.dir.mn/!io
m!mortzloaner!io
m!cadgywage.com!io
m!knelllank.com!io
m!seriessearch.com!io
m!listtimex.com!io
m!litanyfd.com!io
m!15percents.com!io
m!cambrewells.com!io
m!oddbackle.com!io
m!masternix.com!io
m!AGA Resources!io
m!socialsituation.com!io
m!prizeever.com!io
m!Falcon Energy!io
m!healtyupeoples!io
m!cockilyah!io
m!sexydg!io
m!dryishmm!io
m!suffering from cancer!io
m!lutetiaaj!io
m!erbal solution !io
m!qqualitytiimes!io
m!Watch Replica!io
m!premium watch!io
m! escrow company!io
m!westernpay!io
m!ld-post.biz!io
m!stock news!io
m!popingude.com!io
m!greatnumberz.com!io
m!Check the site out!io
m!Only members of the mailing list can post!o
m!searching-for-lov!io
m!Tempus Fugit!o
m!makanjj.info!o
m!Beitling!io
m!Bvlgari!io
m!Third And FINAL Notif!io
m!lieshag\.com!io
m!futhorcib\.com!io
m!contains confidential information and is intended solely for the use of the individual!o
m!pharmacy!io
m!Eighth Real-Time Linux Workshop!o
m!INCOMODANDO!io
m!REMOVEMOS!io
m!morelstore.com!io
m!keessite.com!io
m!yougreattop.com!io
m!snip.ws/!io
m!Start Winning Now!io
m!freamert.com!io
m!ikelterto.com!io
m!Enhanced male power!io
m!seelygd.com!io
m!Impress your girl!io
m!/mj_confirm/!o
m!lorned.com!o
m!/gall/!o
m!/gal/!o
m!/dating/!o
m! stock promotion !io
m!Your credit !io
m!Goldmark Industries!o
m!demandroll.com!o
m!broadcastmarketing!o
m!side effectts!o
m!Zimbabwe!io
m!online partner!io
m!getmeout\@!io
m! multi-orgasmic!o
m!simurl\.com!o
m!armhakirhealth.com!o
m!erritans.com/!o
m!nutrition!io
m!Thermogenesis!io
m! rent.mort !io
m!jiliubabusidvadebila!o
m!templeloan.info!o
m!want an online business!io
m!urlbee.com!io
m!Fatblaster!io
m!originative-products.com!o
m!sanama-intl!o
m!Human Growth Hormone!io
m!retilfo.com!o
m!DVD-TV-ONLINE!io
m!kiladeramirezako!o
m!cmnap.com!o
m!coshp.com!o
m!matenis.com!o
m!miderto.com!o
m!male body image!o
m!justwaitingforyou.com!o
m!turtlkecrazediet.com!o
#m!Online Journaled File System!o
#m!OJFS!o
m!Thank you for your inquiry on IEEE Standards!o
m!Proof-Reading-Service!o
m!TSmtpRelayServer!o
m!bloggoo.com!o
m!No Message Collected!o
END

#TABOO-END
